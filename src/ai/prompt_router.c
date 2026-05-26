#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>
#include "prompt_router.h"
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"
#include "config.h"
#include "observability.h"

#define AI_WORKER_THREADS 4
#define AI_WORK_QUEUE_SIZE 256
#define QUARANTINE_DURATION 60
#define CIRCUIT_BREAK_THRESHOLD 3
#define CIRCUIT_OPEN_DURATION 30
#define CIRCUIT_WINDOW_US 60000000
#define HALF_OPEN_MAX 1
#define CONNECTION_POOL_SIZE 4

typedef enum {
    PROVIDER_OPENAI,
    PROVIDER_ANTHROPIC,
    PROVIDER_GEMINI,
    PROVIDER_DEEPSEEK,
    PROVIDER_CUSTOM
} ProviderType;

typedef struct {
    char *name;
    char *api_endpoint;
    char *api_key_env;
    char provider[32];
    int tier;
    ProviderType provider_type;
    int max_tokens;
    float temperature;
    int is_available;
    int max_retries;
    pthread_mutex_t mutex;
    uint64_t total_latency_us;
    uint64_t total_calls;
    uint64_t last_latency_us;
    uint64_t min_latency_us;
    uint64_t max_latency_us;
    time_t last_health_check;
    int health_ok;
    ModelHealth health;
    CapabilityFlags caps;
    TokenAccount account;
    CURL *cached_curl;
    time_t curl_last_used;
} AIModel;

typedef struct AIWorkItem {
    AIModel *model;
    char *prompt;
    char *response;
    size_t response_size;
    int result;
    AIErrorCode error_code;
    int done;
    pthread_cond_t cond;
    pthread_mutex_t done_mutex;
    struct AIWorkItem *next;
} AIWorkItem;

typedef struct {
    AIModel *models;
    int model_count;
    int model_capacity;
    pthread_mutex_t mutex;
    char *default_model;
    int verify_ssl;
    int smart_routing;
    AIWorkItem *work_queue;
    pthread_mutex_t work_mutex;
    pthread_cond_t work_cond;
    pthread_t worker_threads[AI_WORKER_THREADS];
    int worker_count;
    int workers_running;
} PromptRouter;

static PromptRouter global_router;
static int global_initialized = 0;
static CURLSH *global_curl_share = NULL;

static int send_to_model(AIModel *model, const char *prompt, char *response, size_t response_size, AIErrorCode *error_code);

static void *ai_worker_loop(void *arg) {
    (void)arg;
    while (global_router.workers_running) {
        AIWorkItem *item = NULL;
        pthread_mutex_lock(&global_router.work_mutex);
        while (!global_router.work_queue && global_router.workers_running) {
            struct timespec ts = {0, 50000000};
            pthread_cond_timedwait(&global_router.work_cond, &global_router.work_mutex, &ts);
        }
        if (!global_router.workers_running) { pthread_mutex_unlock(&global_router.work_mutex); break; }
        item = global_router.work_queue;
        if (item) global_router.work_queue = item->next;
        pthread_mutex_unlock(&global_router.work_mutex);

        if (item) {
            item->result = send_to_model(item->model, item->prompt, item->response, item->response_size, &item->error_code);
            pthread_mutex_lock(&item->done_mutex);
            item->done = 1;
            pthread_cond_signal(&item->cond);
            pthread_mutex_unlock(&item->done_mutex);
        }
    }
    return NULL;
}

static int provider_is_eligible(AIModel *model) {
    time_t now = time(NULL);
    switch (model->health.state) {
        case MODEL_STATE_HEALTHY:
        case MODEL_STATE_DEGRADED:
            return 1;
        case MODEL_STATE_RATE_LIMITED:
            if (model->health.quarantined_until > now) return 0;
            model->health.state = MODEL_STATE_HEALTHY;
            return 1;
        case MODEL_STATE_QUARANTINED:
            if (model->health.quarantined_until > now) return 0;
            model->health.state = MODEL_STATE_HEALTHY;
            model->health.consecutive_failures = 0;
            return 1;
        case MODEL_STATE_DECOMMISSIONED:
        case MODEL_STATE_DISABLED:
        case MODEL_STATE_MISSING_KEY:
            return 0;
    }
    return 0;
}

static int is_model_usable(AIModel *model) {
    time_t now = time(NULL);
    if (model->health.state == MODEL_STATE_DECOMMISSIONED || model->health.state == MODEL_STATE_DISABLED) return 0;
    if (model->health.state == MODEL_STATE_MISSING_KEY) return 0;
    if (model->health.quarantined_until > now) return 0;
    if (model->health.circuit_open && model->health.circuit_open_until > now) return 0;
    if (model->health.state == MODEL_STATE_RATE_LIMITED) return 0;
    if (model->health.state == MODEL_STATE_QUARANTINED && model->health.quarantined_until > now) return 0;
    return 1;
}

static double compute_model_score(AIModel *m) {
    double latency_score = 0.0, error_score = 0.0, health_score = 0.0;
    if (m->total_calls > 0) {
        double avg_lat = (double)m->total_latency_us / m->total_calls;
        latency_score = avg_lat / 1000000.0;
        error_score = 0.0;
    } else {
        latency_score = 1.0;
        error_score = 0.5;
    }
    switch (m->health.state) {
        case MODEL_STATE_HEALTHY:   health_score = 0.0; break;
        case MODEL_STATE_DEGRADED:  health_score = 2.0; break;
        case MODEL_STATE_RATE_LIMITED: health_score = 5.0; break;
        default: health_score = 10.0; break;
    }
    return latency_score * 0.5 + error_score * 0.3 + health_score * 0.2;
}

static int submit_ai_work(AIModel *model, const char *prompt, char *response, size_t response_size) {
    AIWorkItem *item = malloc(sizeof(AIWorkItem));
    if (!item) return -1;
    memset(item, 0, sizeof(AIWorkItem));
    item->model = model;
    item->prompt = strdup(prompt);
    item->response = response;
    item->response_size = response_size;
    item->done = 0;
    item->error_code = AI_ERROR_NONE;
    pthread_cond_init(&item->cond, NULL);
    pthread_mutex_init(&item->done_mutex, NULL);

    pthread_mutex_lock(&global_router.work_mutex);
    item->next = global_router.work_queue;
    global_router.work_queue = item;
    pthread_cond_signal(&global_router.work_cond);
    pthread_mutex_unlock(&global_router.work_mutex);

    pthread_mutex_lock(&item->done_mutex);
    while (!item->done) {
        pthread_cond_wait(&item->cond, &item->done_mutex);
    }
    pthread_mutex_unlock(&item->done_mutex);

    int result = item->result;
    free(item->prompt);
    pthread_cond_destroy(&item->cond);
    pthread_mutex_destroy(&item->done_mutex);
    free(item);
    return result;
}

struct MemoryStruct { char *memory; size_t size; };

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->memory = ptr;
    memcpy_dispatch(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

static void extract_provider_name(AIModel *model) {
    const char *url = model->api_endpoint;
    if (strstr(url, "api.groq.com")) snprintf(model->provider, sizeof(model->provider), "groq");
    else if (strstr(url, "api.openai.com")) snprintf(model->provider, sizeof(model->provider), "openai");
    else if (strstr(url, "api.anthropic.com")) snprintf(model->provider, sizeof(model->provider), "anthropic");
    else if (strstr(url, "generativelanguage")) snprintf(model->provider, sizeof(model->provider), "gemini");
    else if (strstr(url, "api.deepseek.com")) snprintf(model->provider, sizeof(model->provider), "deepseek");
    else if (strstr(url, "api.moonshot.cn")) snprintf(model->provider, sizeof(model->provider), "moonshot");
    else if (strstr(url, "open.bigmodel.cn")) snprintf(model->provider, sizeof(model->provider), "zhipu");
    else if (strstr(url, "api.perplexity.ai")) snprintf(model->provider, sizeof(model->provider), "perplexity");
    else if (strstr(url, "api.mistral.ai")) snprintf(model->provider, sizeof(model->provider), "mistral");
    else if (strstr(url, "localhost")) snprintf(model->provider, sizeof(model->provider), "local");
    else snprintf(model->provider, sizeof(model->provider), "unknown");
}

static void detect_provider_type(AIModel *model) {
    const char *url = model->api_endpoint;
    if (strstr(url, "anthropic") || strstr(url, "claude")) model->provider_type = PROVIDER_ANTHROPIC;
    else if (strstr(url, "generativelanguage") || strstr(url, "gemini")) model->provider_type = PROVIDER_GEMINI;
    else if (strstr(url, "deepseek")) model->provider_type = PROVIDER_DEEPSEEK;
    else model->provider_type = PROVIDER_OPENAI;
}

static int estimate_tokens(const char *prompt) {
    if (!prompt || !*prompt) return 0;
    size_t len = strlen(prompt);
    int tokens = 0;
    int in_word = 0;
    int char_count = 0;
    for (const char *p = prompt; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            if (in_word) { tokens++; in_word = 0; char_count = 0; }
        } else {
            in_word = 1;
            char_count++;
            if (char_count >= 4) { tokens++; char_count = 0; }
        }
    }
    if (in_word) tokens++;
    if (len > 0) {
        int est = (int)(len * 0.35f);
        if (est > tokens) tokens = est;
    }
    return tokens + 4;
}

static char *build_openai_payload(const char *model_name, const char *prompt, float temp, int max_tokens) {
    size_t len = strlen(prompt) + 512;
    char *json = malloc(len);
    if (!json) return NULL;
    snprintf(json, len,
        "{\"model\": \"%s\", \"messages\": [{\"role\": \"user\", \"content\": \"%s\"}], "
        "\"temperature\": %.2f, \"max_tokens\": %d}",
        model_name, prompt, temp, max_tokens);
    return json;
}

static char *build_anthropic_payload(const char *prompt, float temp, int max_tokens) {
    size_t len = strlen(prompt) + 512;
    char *json = malloc(len);
    if (!json) return NULL;
    snprintf(json, len,
        "{\"anthropic_version\": \"bedrock-2023-05-31\", \"max_tokens\": %d, "
        "\"messages\": [{\"role\": \"user\", \"content\": \"%s\"}], \"temperature\": %.2f}",
        max_tokens, prompt, temp);
    return json;
}

static char *build_gemini_payload(const char *prompt, float temp, int max_tokens) {
    size_t len = strlen(prompt) + 512;
    char *json = malloc(len);
    if (!json) return NULL;
    snprintf(json, len,
        "{\"contents\": [{\"parts\": [{\"text\": \"%s\"}]}], "
        "\"generationConfig\": {\"temperature\": %.2f, \"maxOutputTokens\": %d}}",
        prompt, temp, max_tokens);
    return json;
}

static int parse_ai_response(const char *raw_response, size_t raw_len, char *output, size_t output_size) {
    if (!raw_response || !output || output_size == 0 || raw_len == 0) return -1;
    const char *end = raw_response + raw_len;
    const char *content_start = (const char *)memmem(raw_response, raw_len, "\"content\":", 10);
    if (!content_start) content_start = (const char *)memmem(raw_response, raw_len, "\"text\":", 7);
    if (!content_start) content_start = (const char *)memmem(raw_response, raw_len, "\"response\":", 11);
    if (!content_start) return -1;
    const char *colon = (const char *)memchr(content_start, ':', end - content_start);
    if (!colon) return -1;
    const char *value_start = colon + 1;
    while (value_start < end && (*value_start == ' ' || *value_start == '\t')) value_start++;
    if (value_start >= end || *value_start != '"') return -1;
    value_start++;
    size_t i = 0;
    const char *p = value_start;
    while (p < end && *p != '"' && i < output_size - 1) {
        if (*p == '\\' && (p + 1) < end) {
            p++;
            switch (*p) {
                case 'n': output[i++] = '\n'; p++; continue;
                case 't': output[i++] = '\t'; p++; continue;
                case 'r': output[i++] = '\r'; p++; continue;
                case '"': output[i++] = '"'; p++; continue;
                case '\\': output[i++] = '\\'; p++; continue;
                default: output[i++] = '\\'; output[i++] = *p; p++; continue;
            }
        }
        output[i++] = *p++;
    }
    output[i] = '\0';
    return 0;
}

static AIErrorCode classify_error(const char *response_body, CURLcode curl_res) {
    if (curl_res == CURLE_OPERATION_TIMEDOUT) return AI_ERROR_TIMEOUT;
    if (curl_res == CURLE_COULDNT_CONNECT) return AI_ERROR_API_FAILURE;
    if (curl_res == CURLE_HTTP_RETURNED_ERROR) {
        if (strstr(response_body, "decommissioned")) return AI_ERROR_DECOMMISSIONED;
        if (strstr(response_body, "rate_limit") || strstr(response_body, "Rate limit") || strstr(response_body, "rate limit")) return AI_ERROR_RATE_LIMIT;
        if (strstr(response_body, "auth") || strstr(response_body, "unauthorized") || strstr(response_body, "invalid_api_key")) return AI_ERROR_AUTH;
        if (strstr(response_body, "not found") || strstr(response_body, "model_not_found")) return AI_ERROR_INVALID_MODEL;
        return AI_ERROR_API_FAILURE;
    }
    if (curl_res != CURLE_OK) return AI_ERROR_API_FAILURE;
    if (response_body && (strstr(response_body, "error"))) {
        if (strstr(response_body, "decommissioned")) return AI_ERROR_DECOMMISSIONED;
        if (strstr(response_body, "rate_limit")) return AI_ERROR_RATE_LIMIT;
    }
    return AI_ERROR_NONE;
}

static void update_model_health(AIModel *model, int success, AIErrorCode error_code) {
    time_t now = time(NULL);
    if (success) {
        model->health.consecutive_failures = 0;
        model->health.consecutive_successes++;
        model->health.last_success_time = now;
        if (model->health.circuit_open && model->health.consecutive_successes >= HALF_OPEN_MAX) {
            model->health.circuit_open = 0;
            model->health.circuit_open_until = 0;
            model->health.circuit_failures = 0;
            model->health.circuit_window_start_us = 0;
            model->health.state = MODEL_STATE_HEALTHY;
            model->health.quarantined_until = 0;
            model->health_ok = 1;
        }
        if (error_code == AI_ERROR_RATE_LIMIT) {
            model->health.state = MODEL_STATE_RATE_LIMITED;
            model->health.quarantined_until = now + 5;
        } else if (model->health.state == MODEL_STATE_RATE_LIMITED) {
            model->health.state = MODEL_STATE_HEALTHY;
            model->health.quarantined_until = 0;
        }
    } else {
        model->health.consecutive_failures++;
        model->health.consecutive_successes = 0;
        model->health.last_failure_time = now;
        if (error_code == AI_ERROR_DECOMMISSIONED) {
            model->health.state = MODEL_STATE_DECOMMISSIONED;
            model->health_ok = 0;
            char log[256];
            snprintf(log, sizeof(log), "[LIFECYCLE] Model %s marked DECOMMISSIONED", model->name);
            log_message("AI_ROUTER", log);
            return;
        }
        if (error_code == AI_ERROR_RATE_LIMIT) {
            model->health.state = MODEL_STATE_RATE_LIMITED;
            model->health.quarantined_until = now + 10;
            model->health_ok = 0;
            return;
        }
        uint64_t now_us = (uint64_t)now * 1000000;
        if (model->health.circuit_window_start_us == 0) {
            model->health.circuit_window_start_us = now_us;
            model->health.circuit_failures = 1;
        } else if (now_us - model->health.circuit_window_start_us < CIRCUIT_WINDOW_US) {
            model->health.circuit_failures++;
            if (model->health.circuit_failures >= CIRCUIT_BREAK_THRESHOLD) {
                model->health.circuit_open = 1;
                model->health.circuit_open_until = now + CIRCUIT_OPEN_DURATION;
                model->health.state = MODEL_STATE_QUARANTINED;
                model->health_ok = 0;
                char log[256];
                snprintf(log, sizeof(log), "[CIRCUIT] Model %s circuit OPEN for %ds (%d failures in window)",
                         model->name, CIRCUIT_OPEN_DURATION, model->health.circuit_failures);
                log_message("AI_ROUTER", log);
                return;
            }
        } else {
            model->health.circuit_window_start_us = now_us;
            model->health.circuit_failures = 1;
        }
        if (model->health.consecutive_failures >= 2) {
            model->health.state = MODEL_STATE_QUARANTINED;
            model->health.quarantined_until = now + QUARANTINE_DURATION;
            model->health_ok = 0;
        } else {
            model->health_ok = 0;
        }
    }
}

static CURL *get_curl_handle(AIModel *model) {
    time_t now = time(NULL);
    if (model->cached_curl && (now - model->curl_last_used) < 30) {
        model->curl_last_used = now;
        curl_easy_reset(model->cached_curl);
        return model->cached_curl;
    }
    if (model->cached_curl) curl_easy_cleanup(model->cached_curl);
    model->cached_curl = curl_easy_init();
    model->curl_last_used = now;
    if (model->cached_curl && global_curl_share) {
        curl_easy_setopt(model->cached_curl, CURLOPT_SHARE, global_curl_share);
    }
    return model->cached_curl;
}

static void return_curl_handle(AIModel *model, CURL *curl) {
    (void)curl;
    model->curl_last_used = time(NULL);
}

static int send_to_model(AIModel *model, const char *prompt, char *response, size_t response_size, AIErrorCode *out_error_code) {
    if (!model || !prompt || !response || response_size == 0) return -1;
    *out_error_code = AI_ERROR_NONE;

    pthread_mutex_lock(&model->mutex);

    if (!is_model_usable(model)) {
        if (model->health.state == MODEL_STATE_DECOMMISSIONED) {
            *out_error_code = AI_ERROR_DECOMMISSIONED;
            snprintf(response, response_size, "{\"error\": \"Model %s has been decommissioned\"}", model->name);
            pthread_mutex_unlock(&model->mutex);
            return -1;
        }
        if (model->health.circuit_open) {
            *out_error_code = AI_ERROR_CIRCUIT_OPEN;
            snprintf(response, response_size, "{\"error\": \"Model %s circuit is open, try again later\"}", model->name);
            pthread_mutex_unlock(&model->mutex);
            return -1;
        }
        if (model->health.quarantined_until > time(NULL)) {
            *out_error_code = AI_ERROR_QUARANTINED;
            snprintf(response, response_size, "{\"error\": \"Model %s is quarantined\"}", model->name);
            pthread_mutex_unlock(&model->mutex);
            return -1;
        }
        if (model->health.state == MODEL_STATE_RATE_LIMITED) {
            *out_error_code = AI_ERROR_RATE_LIMIT;
            snprintf(response, response_size, "{\"error\": \"Model %s is rate limited\"}", model->name);
            pthread_mutex_unlock(&model->mutex);
            return -1;
        }
    }

    CURL *curl = get_curl_handle(model);
    if (!curl) { pthread_mutex_unlock(&model->mutex); return -1; }

    struct MemoryStruct chunk = {NULL, 0};
    chunk.memory = malloc(1);
    if (!chunk.memory) { pthread_mutex_unlock(&model->mutex); return -1; }
    chunk.size = 0;

    char *json_payload = NULL;
    switch (model->provider_type) {
        case PROVIDER_ANTHROPIC: json_payload = build_anthropic_payload(prompt, model->temperature, model->max_tokens); break;
        case PROVIDER_GEMINI: json_payload = build_gemini_payload(prompt, model->temperature, model->max_tokens); break;
        default: json_payload = build_openai_payload(model->name, prompt, model->temperature, model->max_tokens); break;
    }
    if (!json_payload) { free(chunk.memory); pthread_mutex_unlock(&model->mutex); return -1; }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char *api_key = getenv(model->api_key_env);
    if (api_key) {
        char auth_header[512];
        if (model->provider_type == PROVIDER_ANTHROPIC) {
            snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
            headers = curl_slist_append(headers, auth_header);
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        } else {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
            headers = curl_slist_append(headers, auth_header);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, model->api_endpoint);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    if (global_router.verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "[SEND] model=%s provider=%s tier=%d", model->name, model->provider, model->tier);
    log_message("AI_ROUTER", log_msg);

    CURLcode res;
    int retry_count = 0;
    int max_retries = model->max_retries > 0 ? model->max_retries : 2;
    uint64_t start_us = get_current_time_us();

    do {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;
        long http_code = 0;
        if (res == CURLE_HTTP_RETURNED_ERROR) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 401 || http_code == 403) { retry_count = max_retries + 1; break; }
            if (http_code == 404) { retry_count = max_retries + 1; break; }
            if (http_code == 429) { retry_count = max_retries + 1; break; }
            if (http_code == 500 || http_code == 502 || http_code == 503) {
                retry_count++;
                if (retry_count <= max_retries) {
                    int delay_ms = 500 * (1 << (retry_count - 1)) + (rand() % 200);
                    struct timespec ts = {delay_ms / 1000, (delay_ms % 1000) * 1000000};
                    nanosleep(&ts, NULL);
                }
                continue;
            }
        }
        if (res == CURLE_OPERATION_TIMEDOUT) {
            retry_count++;
            if (retry_count <= max_retries) {
                int delay_ms = 100 + (rand() % 100);
                struct timespec ts = {delay_ms / 1000, (delay_ms % 1000) * 1000000};
                nanosleep(&ts, NULL);
            }
            continue;
        }
        retry_count++;
        if (retry_count <= max_retries) {
            int delay_ms = 100 * (1 << (retry_count - 1)) + (rand() % 50);
            struct timespec ts = {delay_ms / 1000, (delay_ms % 1000) * 1000000};
            nanosleep(&ts, NULL);
        }
    } while (retry_count <= max_retries);

    uint64_t latency_us = get_current_time_us() - start_us;
    model->total_latency_us += latency_us;
    model->total_calls++;
    model->last_latency_us = latency_us;
    if (latency_us < model->min_latency_us || model->min_latency_us == 0) model->min_latency_us = latency_us;
    if (latency_us > model->max_latency_us) model->max_latency_us = latency_us;

    AIErrorCode error_code = AI_ERROR_NONE;
    if (res != CURLE_OK) {
        error_code = classify_error(chunk.memory ? chunk.memory : "", res);
        snprintf(response, response_size, "{\"error\": \"API request failed: %s\"}", curl_easy_strerror(res));
        model->account.total_errors++;
        model->account.total_requests++;
        update_model_health(model, 0, error_code);
        obs_record_ai_call(model->provider, model->name, latency_us, 0, 0, 1, error_code);
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code >= 400 && chunk.memory) {
            error_code = classify_error(chunk.memory, CURLE_HTTP_RETURNED_ERROR);
            strncpy(response, chunk.memory, response_size);
            response[response_size - 1] = '\0';
            model->account.total_errors++;
            model->account.total_requests++;
            update_model_health(model, 0, error_code);
            obs_record_ai_call(model->provider, model->name, latency_us, 0, 0, 1, error_code);
        } else {
            model->health_ok = 1;
            model->last_health_check = time(NULL);
            if (parse_ai_response(chunk.memory, chunk.size, response, response_size) != 0) {
                strncpy(response, chunk.memory, response_size);
                response[response_size - 1] = '\0';
            }
            uint64_t in_tok = (uint64_t)estimate_tokens(prompt);
            uint64_t out_tok = (uint64_t)estimate_tokens(response);
            model->account.total_input_tokens += in_tok;
            model->account.total_output_tokens += out_tok;
            model->account.total_requests++;
            model->account.total_cost_usd += (in_tok * 5 + out_tok * 15) / 1000000;
            update_model_health(model, 1, AI_ERROR_NONE);
            obs_record_ai_call(model->provider, model->name, latency_us, in_tok, out_tok, 0, AI_ERROR_NONE);
        }
    }

    *out_error_code = error_code;
    free(chunk.memory);
    free(json_payload);
    curl_slist_free_all(headers);
    return_curl_handle(model, curl);
    pthread_mutex_unlock(&model->mutex);
    return (res == CURLE_OK) ? 0 : -1;
}

int prompt_router_init_with_config(const Config *config) {
    curl_global_init(CURL_GLOBAL_ALL);
    global_curl_share = curl_share_init();
    if (global_curl_share) {
        curl_share_setopt(global_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        curl_share_setopt(global_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(global_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    }

    global_router.model_capacity = 64;
    global_router.models = calloc(global_router.model_capacity, sizeof(AIModel));
    if (!global_router.models) return -1;
    global_router.model_count = 0;
    global_router.default_model = NULL;
    global_router.verify_ssl = config ? config->verify_ssl : 1;
    global_router.smart_routing = config ? config->enable_smart_routing : 1;

    if (pthread_mutex_init(&global_router.mutex, NULL) != 0) { free(global_router.models); return -1; }

    if (config && config->ai_models) {
        for (int i = 0; i < config->ai_model_count; i++) {
            AIModelConfig *mc = &config->ai_models[i];
            prompt_router_add_model(mc->name, mc->api_endpoint,
                mc->api_key_env ? mc->api_key_env : "OPENAI_API_KEY",
                mc->max_tokens, mc->temperature, mc->tier);
        }
    } else {
        prompt_router_add_model("llama-3.3-70b-versatile", "https://api.groq.com/openai/v1/chat/completions", "GROQ_API_KEY", 8192, 0.7, 2);
        prompt_router_add_model("llama-3.1-8b-instant", "https://api.groq.com/openai/v1/chat/completions", "GROQ_API_KEY", 8192, 0.7, 1);
        prompt_router_add_model("gemma2-9b-it", "https://api.groq.com/openai/v1/chat/completions", "GROQ_API_KEY", 8192, 0.7, 1);
    }

    if (!global_router.default_model) global_router.default_model = strdup("llama-3.3-70b-versatile");

    int available = 0;
    for (int i = 0; i < global_router.model_count; i++) {
        if (global_router.models[i].is_available) available++;
    }
    char avail_log[256];
    snprintf(avail_log, sizeof(avail_log), "Models loaded: %d total, %d available (keys detected), smart_routing=%s",
             global_router.model_count, available,
             global_router.smart_routing ? "on" : "off");
    log_message("AI_ROUTER", avail_log);

    global_router.workers_running = 1;
    pthread_mutex_init(&global_router.work_mutex, NULL);
    pthread_cond_init(&global_router.work_cond, NULL);
    global_router.worker_count = AI_WORKER_THREADS;
    for (int i = 0; i < global_router.worker_count; i++) {
        pthread_create(&global_router.worker_threads[i], NULL, ai_worker_loop, NULL);
    }

    log_message("AI_ROUTER", "Prompt router initialized with multi-provider support");
    global_initialized = 1;
    return 0;
}

int prompt_router_init(void) { return prompt_router_init_with_config(NULL); }

int prompt_router_add_model(const char *name, const char *api_endpoint, const char *api_key_env, int max_tokens, float temperature, int tier) {
    if (!name || !api_endpoint) return -1;
    pthread_mutex_lock(&global_router.mutex);
    if (global_router.model_count >= global_router.model_capacity) {
        int new_cap = global_router.model_capacity * 2;
        AIModel *new_models = realloc(global_router.models, sizeof(AIModel) * new_cap);
        if (!new_models) { pthread_mutex_unlock(&global_router.mutex); return -1; }
        memset(new_models + global_router.model_capacity, 0, sizeof(AIModel) * (new_cap - global_router.model_capacity));
        global_router.models = new_models;
        global_router.model_capacity = new_cap;
    }
    AIModel *model = &global_router.models[global_router.model_count];
    memset(model, 0, sizeof(AIModel));
    model->name = strdup(name);
    model->api_endpoint = strdup(api_endpoint);
    model->api_key_env = strdup(api_key_env ? api_key_env : "OPENAI_API_KEY");
    model->max_tokens = max_tokens > 0 ? max_tokens : 4096;
    model->temperature = temperature;
    model->tier = (tier >= 1 && tier <= 4) ? tier : 2;
    model->max_retries = 2;
    model->health_ok = 1;
    model->total_calls = 0;
    model->total_latency_us = 0;
    model->min_latency_us = 0;
    model->max_latency_us = 0;
    model->health.state = MODEL_STATE_HEALTHY;
    model->health.circuit_open = 0;
    model->cached_curl = NULL;
    model->curl_last_used = 0;
    extract_provider_name(model);
    detect_provider_type(model);
    const char *key_val = getenv(model->api_key_env);
    model->is_available = (key_val && key_val[0] != '\0') ? 1 : 0;
    if (!model->is_available) model->health.state = MODEL_STATE_MISSING_KEY;
    if (pthread_mutex_init(&model->mutex, NULL) != 0) {
        free(model->name); free(model->api_endpoint); free(model->api_key_env);
        pthread_mutex_unlock(&global_router.mutex); return -1;
    }
    global_router.model_count++;
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Model added: %s [tier=%d provider=%s key=%s]",
             name, model->tier, model->provider, model->is_available ? "SET" : "MISSING");
    log_message("AI_ROUTER", log_msg);
    pthread_mutex_unlock(&global_router.mutex);
    return 0;
}

int prompt_router_add_model_ex(const char *name, const char *api_endpoint, const char *api_key_env, int max_tokens, float temperature, int tier, CapabilityFlags caps) {
    int ret = prompt_router_add_model(name, api_endpoint, api_key_env, max_tokens, temperature, tier);
    if (ret == 0) {
        pthread_mutex_lock(&global_router.mutex);
        for (int i = 0; i < global_router.model_count; i++) {
            if (strcmp(global_router.models[i].name, name) == 0) {
                global_router.models[i].caps = caps;
                break;
            }
        }
        pthread_mutex_unlock(&global_router.mutex);
    }
    return ret;
}

int prompt_router_get_token_account(TokenAccount *account) {
    if (!account) return -1;
    memset(account, 0, sizeof(TokenAccount));
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        account->total_input_tokens += global_router.models[i].account.total_input_tokens;
        account->total_output_tokens += global_router.models[i].account.total_output_tokens;
        account->total_cost_usd += global_router.models[i].account.total_cost_usd;
        account->total_requests += global_router.models[i].account.total_requests;
        account->total_errors += global_router.models[i].account.total_errors;
    }
    pthread_mutex_unlock(&global_router.mutex);
    return 0;
}

int prompt_router_hot_reload(void) {
    log_message("AI_ROUTER", "Hot reload triggered (re-checking API keys)...");
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        const char *key_val = getenv(global_router.models[i].api_key_env);
        int had_key = global_router.models[i].is_available;
        global_router.models[i].is_available = (key_val && key_val[0] != '\0') ? 1 : 0;
        if (!global_router.models[i].is_available) {
            global_router.models[i].health.state = MODEL_STATE_MISSING_KEY;
        } else if (global_router.models[i].health.state == MODEL_STATE_MISSING_KEY) {
            global_router.models[i].health.state = MODEL_STATE_HEALTHY;
        }
        if (had_key != global_router.models[i].is_available) {
            char log[256];
            snprintf(log, sizeof(log), "[HOT-RELOAD] %s: key %s -> %s", global_router.models[i].name,
                     global_router.models[i].api_key_env,
                     global_router.models[i].is_available ? "SET" : "MISSING");
            log_message("AI_ROUTER", log);
        }
    }
    pthread_mutex_unlock(&global_router.mutex);
    log_message("AI_ROUTER", "Hot reload complete");
    return 0;
}

int prompt_router_remove_model(const char *name) {
    if (!name) return -1;
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            AIModel *model = &global_router.models[i];
            pthread_mutex_destroy(&model->mutex);
            if (model->cached_curl) curl_easy_cleanup(model->cached_curl);
            free(model->name); free(model->api_endpoint); free(model->api_key_env);
            for (int j = i; j < global_router.model_count - 1; j++) global_router.models[j] = global_router.models[j + 1];
            global_router.model_count--;
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

int prompt_router_set_default_model(const char *name) {
    if (!name) return -1;
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            if (global_router.default_model) free(global_router.default_model);
            global_router.default_model = strdup(name);
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

int prompt_router_route(const char *prompt, const char *model_name, char *response, size_t response_size, char *actual_model, size_t actual_model_size) {
    if (!prompt || !response || response_size == 0) return -1;

    int est_tokens = estimate_tokens(prompt);

    pthread_mutex_lock(&global_router.mutex);

    AIModel *candidates[64];
    int candidate_count = 0;

    if (model_name && model_name[0] != '\0') {
        AIModel *requested = NULL;
        for (int i = 0; i < global_router.model_count; i++) {
            if (strcmp(global_router.models[i].name, model_name) == 0) {
                requested = &global_router.models[i];
                if (!provider_is_eligible(requested)) {
                    char warn[256];
                    snprintf(warn, sizeof(warn), "[ELIGIBILITY] Model %s requested but ineligible (state=%d)",
                             model_name, requested->health.state);
                    log_message("AI_ROUTER", warn);
                }
                candidates[candidate_count++] = requested;
                break;
            }
        }
        if (!requested) {
            for (int i = 0; i < global_router.model_count; i++) {
                if (strcmp(global_router.models[i].name, model_name) == 0) {
                    memset(&global_router.models[i].health, 0, sizeof(ModelHealth));
                    global_router.models[i].health.state = MODEL_STATE_HEALTHY;
                    global_router.models[i].is_available = 1;
                    global_router.models[i].health_ok = 1;
                    requested = &global_router.models[i];
                    candidates[candidate_count++] = requested;
                    break;
                }
            }
        }
        if (requested && !provider_is_eligible(requested)) {
            for (int i = 0; i < global_router.model_count; i++) {
                if (&global_router.models[i] != requested && provider_is_eligible(&global_router.models[i])) {
                    candidates[candidate_count++] = &global_router.models[i];
                    if (candidate_count >= 64) break;
                }
            }
        }
    } else if (global_router.smart_routing) {
        AIModel *scored[64];
        int scored_count = 0;
        for (int i = 0; i < global_router.model_count; i++) {
            if (provider_is_eligible(&global_router.models[i])) {
                scored[scored_count++] = &global_router.models[i];
            }
        }
        for (int i = 0; i < scored_count; i++) {
            for (int j = i + 1; j < scored_count; j++) {
                double si = compute_model_score(scored[i]);
                double sj = compute_model_score(scored[j]);
                if (si > sj) {
                    AIModel *tmp = scored[i]; scored[i] = scored[j]; scored[j] = tmp;
                }
            }
        }
        for (int i = 0; i < scored_count && candidate_count < 64; i++) {
            if (scored[i]->tier >= 1) {
                candidates[candidate_count++] = scored[i];
            }
        }
        if (candidate_count == 0) {
            for (int i = 0; i < global_router.model_count; i++) {
                if (provider_is_eligible(&global_router.models[i])) {
                    candidates[candidate_count++] = &global_router.models[i];
                    if (candidate_count >= 64) break;
                }
            }
        }
        if (candidate_count == 0) {
            for (int i = 0; i < global_router.model_count; i++) {
                if (strcmp(global_router.models[i].name, global_router.default_model) == 0) {
                    candidates[candidate_count++] = &global_router.models[i];
                    break;
                }
            }
        }
        if (candidate_count == 0 && global_router.model_count > 0) {
            for (int i = 0; i < global_router.model_count; i++) {
                if (global_router.models[i].is_available) {
                    candidates[candidate_count++] = &global_router.models[i];
                    if (candidate_count >= 64) break;
                }
            }
        }
        if (candidate_count == 0 && global_router.model_count > 0) {
            candidates[candidate_count++] = &global_router.models[0];
        }
    } else {
        for (int i = 0; i < global_router.model_count; i++) {
            if (strcmp(global_router.models[i].name, global_router.default_model) == 0) {
                candidates[candidate_count++] = &global_router.models[i];
                break;
            }
        }
        if (candidate_count == 0 && global_router.model_count > 0) {
            candidates[candidate_count++] = &global_router.models[0];
        }
    }

    pthread_mutex_unlock(&global_router.mutex);

    int fallback_logged = 0;
    for (int i = 0; i < candidate_count; i++) {
        AIModel *model = candidates[i];

        if (est_tokens > 0 && est_tokens > model->max_tokens) {
            char warn[256];
            snprintf(warn, sizeof(warn), "[TOKEN-EST] Prompt ~%d tokens exceeds %s max %d",
                     est_tokens, model->name, model->max_tokens);
            log_message("AI_ROUTER", warn);
        }

        int result = submit_ai_work(model, prompt, response, response_size);
        if (result == 0) {
            if (actual_model && actual_model_size > 0) {
                strncpy(actual_model, model->name, actual_model_size - 1);
                actual_model[actual_model_size - 1] = '\0';
            }
            char log_buf[256];
            uint64_t avg_lat = model->total_calls > 0 ?
                model->total_latency_us / model->total_calls : 0;
            snprintf(log_buf, sizeof(log_buf), "[SMART-ROUTER] Model=%s Tier=%d Provider=%s Calls=%lu AvgLat=%luus",
                     model->name, model->tier, model->provider,
                     (unsigned long)model->total_calls, (unsigned long)avg_lat);
            log_message("AI_ROUTER", log_buf);
            return 0;
        }
        if (!fallback_logged) {
            char fail_log[256];
            snprintf(fail_log, sizeof(fail_log), "[FALLBACK] %s failed, trying next model...", model->name);
            log_message("AI_ROUTER", fail_log);
            fallback_logged = 1;
        }
    }

    snprintf(response, response_size, "{\"error\": \"All AI models failed to process request\"}");
    if (actual_model && actual_model_size > 0) actual_model[0] = '\0';
    return -1;
}

int prompt_router_get_models(char ***model_names, int *count) {
    pthread_mutex_lock(&global_router.mutex);
    *count = global_router.model_count;
    *model_names = malloc(sizeof(char *) * (*count));
    if (!*model_names) { pthread_mutex_unlock(&global_router.mutex); return -1; }
    for (int i = 0; i < *count; i++) (*model_names)[i] = strdup(global_router.models[i].name);
    pthread_mutex_unlock(&global_router.mutex);
    return 0;
}

int prompt_router_get_available_count(void) {
    pthread_mutex_lock(&global_router.mutex);
    int count = 0;
    for (int i = 0; i < global_router.model_count; i++) {
        if (global_router.models[i].is_available && is_model_usable(&global_router.models[i])) count++;
    }
    pthread_mutex_unlock(&global_router.mutex);
    return count;
}

int prompt_router_set_model_availability(const char *name, int is_available) {
    if (!name) return -1;
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            global_router.models[i].is_available = is_available;
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

int prompt_router_set_model_state(const char *name, ModelState state) {
    if (!name) return -1;
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            global_router.models[i].health.state = state;
            if (state == MODEL_STATE_DECOMMISSIONED || state == MODEL_STATE_DISABLED) {
                global_router.models[i].health_ok = 0;
            }
            char log[256];
            snprintf(log, sizeof(log), "[LIFECYCLE] Model %s state set to %d", name, state);
            log_message("AI_ROUTER", log);
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

int prompt_router_get_model_health(const char *name, ModelHealth *health) {
    if (!name || !health) return -1;
    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            *health = global_router.models[i].health;
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

void prompt_router_cleanup(void) {
    global_router.workers_running = 0;
    pthread_cond_broadcast(&global_router.work_cond);
    for (int i = 0; i < global_router.worker_count; i++) {
        pthread_join(global_router.worker_threads[i], NULL);
    }
    pthread_mutex_destroy(&global_router.work_mutex);
    pthread_cond_destroy(&global_router.work_cond);

    pthread_mutex_lock(&global_router.mutex);
    for (int i = 0; i < global_router.model_count; i++) {
        pthread_mutex_destroy(&global_router.models[i].mutex);
        if (global_router.models[i].cached_curl) curl_easy_cleanup(global_router.models[i].cached_curl);
        free(global_router.models[i].name);
        free(global_router.models[i].api_endpoint);
        free(global_router.models[i].api_key_env);
    }
    free(global_router.models);
    free(global_router.default_model);
    pthread_mutex_unlock(&global_router.mutex);
    pthread_mutex_destroy(&global_router.mutex);

    if (global_curl_share) curl_share_cleanup(global_curl_share);
    global_curl_share = NULL;
    curl_global_cleanup();
    global_initialized = 0;
    log_message("AI_ROUTER", "Prompt router cleaned up");
}
