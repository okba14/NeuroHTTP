#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include "router.h"
#include "parser.h"
#include "stream.h"
#include "ai/prompt_router.h"
#include "ai/ai_gateway.h"
#include "asm_utils.h"
#include "utils.h"
#include "server.h"
#include "observability.h"
#include "cache.h"

#define MAX_CACHED_RESPONSES 16
#define MAX_MIDDLEWARE 8
#define MAX_ROUTE_PARAMS 8
#define MAX_PROMPT_SIZE 16384
#define MAX_LOG_PREVIEW 100
#define INITIAL_AI_BUF_SIZE 65536

static RouteHashTable routes_table = {0};
static RouteResponse cached_404_response = {0};
static RouteResponse cached_root_response = {0};
static MiddlewareFunc middlewares[MAX_MIDDLEWARE] = {NULL};
static int middleware_count = 0;

static const char* route_error_messages[] = {
    "No error", "Memory allocation failed", "Invalid parameter", "Route not found", "Internal server error"
};

static char* json_escape_str(const char* input) {
    if (!input) return strdup("");
    size_t len = strlen(input);
    char* escaped = malloc((len * 2) + 1);
    if (!escaped) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (input[i]) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            default:
                if ((unsigned char)input[i] < 32) escaped[j++] = ' ';
                else escaped[j++] = input[i];
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

static char* extract_json_value(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    char *key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;
    key_pos += strlen(search_key);
    while (*key_pos && (*key_pos == ' ' || *key_pos == ':')) key_pos++;
    if (*key_pos == '"') {
        key_pos++;
        char *end = strchr(key_pos, '"');
        if (end) {
            size_t len = end - key_pos;
            char *value = malloc(len + 1);
            if (value) { strncpy(value, key_pos, len); value[len] = '\0'; return value; }
        }
    }
    return NULL;
}

static int method_matches(HTTPMethod method1, HTTPMethod method2) { return method1 == method2; }

static int route_matches_with_params(const char *route_path, const char *request_path) {
    char route_copy[256], request_copy[256];
    strncpy(route_copy, route_path, sizeof(route_copy) - 1);
    strncpy(request_copy, request_path, sizeof(request_copy) - 1);
    char *route_token = strtok(route_copy, "/");
    char *request_token = strtok(request_copy, "/");
    while (route_token && request_token) {
        if (route_token[0] != ':' && strcmp(route_token, request_token) != 0) return 0;
        route_token = strtok(NULL, "/");
        request_token = strtok(NULL, "/");
    }
    return !route_token && !request_token;
}

static unsigned int hash_string(const char *str) {
    if (!str || !*str) return 0;
    return crc32_asm(str, strlen(str)) % HASH_TABLE_SIZE;
}

static void hash_table_init(RouteHashTable *table) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) table->buckets[i] = NULL;
    pthread_rwlock_init(&table->lock, NULL);
}

static int hash_table_add(RouteHashTable *table, Route *route) {
    unsigned int index = hash_string(route->path);
    pthread_rwlock_wrlock(&table->lock);
    route->next = table->buckets[index];
    table->buckets[index] = route;
    pthread_rwlock_unlock(&table->lock);
    return 0;
}

static Route *hash_table_find(RouteHashTable *table, const char *path, HTTPMethod method) {
    unsigned int index = hash_string(path);
    pthread_rwlock_rdlock(&table->lock);
    Route *current = table->buckets[index];
    while (current) {
        if (strcmp(current->path, path) == 0 && method_matches(current->method, method)) { pthread_rwlock_unlock(&table->lock); return current; }
        current = current->next;
    }
    pthread_rwlock_unlock(&table->lock);
    return NULL;
}

static Route *hash_table_find_with_params(RouteHashTable *table, const char *path, HTTPMethod method) {
    pthread_rwlock_rdlock(&table->lock);
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        Route *current = table->buckets[i];
        while (current) {
            if (method_matches(current->method, method) && route_matches_with_params(current->path, path)) { pthread_rwlock_unlock(&table->lock); return current; }
            current = current->next;
        }
    }
    pthread_rwlock_unlock(&table->lock);
    return NULL;
}

static void hash_table_free(RouteHashTable *table) {
    pthread_rwlock_wrlock(&table->lock);
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        Route *current = table->buckets[i];
        while (current) { Route *next = current->next; free(current->path); free(current); current = next; }
        table->buckets[i] = NULL;
    }
    pthread_rwlock_unlock(&table->lock);
    pthread_rwlock_destroy(&table->lock);
}

static int create_http_response(RouteResponse *response, const char *body, size_t body_length, const char *content_type, int status_code, const char *status_message) {
    if (!response || !body) return -1;
    time_t now; time(&now);
    struct tm *tm_info = gmtime(&now);
    char date_buf[128];
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    size_t headers_size = 128 + strlen(date_buf) + strlen(content_type) + 32;
    size_t total_size = headers_size + body_length;
    response->data = malloc(total_size);
    if (!response->data) return -1;
    char *ptr = response->data;
    ptr += sprintf(ptr, "HTTP/1.1 %d %s\r\n", status_code, status_message);
    ptr += sprintf(ptr, "Date: %s\r\n", date_buf);
    ptr += sprintf(ptr, "Server: AIONIC/1.0\r\n");
    ptr += sprintf(ptr, "Content-Type: %s\r\n", content_type);
    ptr += sprintf(ptr, "Content-Length: %zu\r\n", body_length);
    ptr += sprintf(ptr, "Connection: keep-alive\r\n");
    ptr += sprintf(ptr, "Access-Control-Allow-Origin: *\r\n\r\n");
    memcpy_dispatch(ptr, body, body_length);
    response->length = (ptr - response->data) + body_length;
    response->status_code = status_code;
    response->status_message = (char *)status_message;
    response->is_streaming = 0;
    return 0;
}

int create_error_response(RouteResponse *response, RouteError error, int status_code) {
    const char *error_message = route_error_messages[error];
    char *json_response = malloc(256);
    if (!json_response) return ROUTE_ERROR_MEMORY;
    snprintf(json_response, 256, "{\"error\": \"%s\", \"code\": %d, \"timestamp\": %ld}", error_message, error, time(NULL));
    int result = create_http_response(response, json_response, strlen(json_response), "application/json", status_code, error_message);
    free(json_response);
    return result;
}

static int copy_route_response(const RouteResponse *source, RouteResponse *dest) {
    if (!source || !dest) return -1;
    dest->data = malloc(source->length);
    if (!dest->data) return -1;
    memcpy_dispatch(dest->data, source->data, source->length);
    dest->length = source->length;
    dest->status_code = source->status_code;
    dest->status_message = source->status_message;
    dest->is_streaming = source->is_streaming;
    return 0;
}

static void init_cached_responses(void) {
    const char *not_found_html =
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
        "<title>404 | LOST IN THE VOID</title><style>"
        ":root{--neon:#ff0055;--bg:#050505;--card:rgba(20,20,25,0.8);}"
        "body{margin:0;height:100vh;background:radial-gradient(circle at center,#1a1a2e 0%,#000 100%);"
        "color:#fff;font-family:'Courier New',monospace;display:flex;align-items:center;justify-content:center;overflow:hidden;}"
        ".container{text-align:center;z-index:2;}h1{font-size:8rem;margin:0;color:transparent;"
        "-webkit-text-stroke:2px var(--neon);text-shadow:0 0 20px var(--neon);animation:glitch 3s infinite;}"
        "h2{font-weight:300;letter-spacing:5px;margin-top:-20px;}p{color:#888;margin-bottom:40px;}"
        ".btn{padding:15px 40px;background:transparent;border:1px solid var(--neon);color:var(--neon);"
        "text-decoration:none;text-transform:uppercase;letter-spacing:2px;transition:0.3s;"
        "box-shadow:0 0 10px rgba(255,0,85,0.2);}.btn:hover{background:var(--neon);color:#000;box-shadow:0 0 40px var(--neon);}"
        ".scanline{position:fixed;left:0;top:0;width:100%;height:100%;"
        "background:linear-gradient(to bottom,rgba(255,255,255,0),rgba(255,255,255,0) 50%,rgba(0,0,0,0.2) 50%,rgba(0,0,0,0.2));"
        "background-size:100% 4px;pointer-events:none;z-index:1;}"
        "@keyframes glitch{0%{transform:skew(0deg);}20%{transform:skew(-2deg);}21%{transform:skew(2deg);}100%{transform:skew(0deg);}}"
        "</style></head><body><div class=\"scanline\"></div><div class=\"container\">"
        "<h1>404</h1><h2>SIGNAL LOST // SEVER NOT FOUND</h2>"
        "<p>The requested coordinates do not exist in this memory block.</p>"
        "<a href=\"/\" class=\"btn\">Reboot System</a></div></body></html>";
    create_http_response(&cached_404_response, not_found_html, strlen(not_found_html), "text/html", 404, "Not Found");
}

int register_route(const char *path, HTTPMethod method, int (*handler)(Server *, HTTPRequest *, RouteResponse *)) {
    Route *route = malloc(sizeof(Route));
    if (!route) return -1;
    route->path = strdup(path);
    route->method = method;
    route->handler = handler;
    route->param_count = 0;
    route->next = NULL;
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    while (token && route->param_count < MAX_ROUTE_PARAMS) {
        if (token[0] == ':') { strncpy(route->param_names[route->param_count], token + 1, 31); route->param_names[route->param_count][31] = '\0'; route->param_count++; }
        token = strtok(NULL, "/");
    }
    free(path_copy);
    return hash_table_add(&routes_table, route);
}

int register_middleware(MiddlewareFunc middleware) {
    if (middleware_count >= MAX_MIDDLEWARE) return -1;
    middlewares[middleware_count++] = middleware;
    return 0;
}

int route_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 400);
    memset(response, 0, sizeof(RouteResponse));
    for (int i = 0; i < middleware_count; i++) {
        int result = middlewares[i](request, response);
        if (result != 0) return result;
        if (response->data) return 0;
    }
    if (strcmp(request->path, "/") == 0 && method_matches(request->method, HTTP_GET)) return copy_route_response(&cached_root_response, response);
    Route *route = hash_table_find(&routes_table, request->path, request->method);
    if (route) return route->handler(server, request, response);
    route = hash_table_find_with_params(&routes_table, request->path, request->method);
    if (route) return route->handler(server, request, response);
    return copy_route_response(&cached_404_response, response);
}

void free_route_response(RouteResponse *response) {
    if (!response) return;
    free(response->data);
    free(response->stream_data);
    for (int i = 0; i < response->header_count; i++) free(response->headers[i]);
    memset(response, 0, sizeof(RouteResponse));
}

int handle_chat_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server;
    if (!request || !response) return -1;
    char *prompt = NULL, *model_name = NULL;
    int status = -1;
    uint64_t start_ns = get_current_time_ns();

    if (!request->body || request->body_length > MAX_PROMPT_SIZE) return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 413);

    prompt = extract_json_value(request->body, "prompt");
    model_name = extract_json_value(request->body, "model");

    if (!prompt) {
        size_t copy_len = (request->body_length < MAX_PROMPT_SIZE) ? request->body_length : MAX_PROMPT_SIZE;
        prompt = malloc(copy_len + 1);
        if (prompt) { strncpy(prompt, request->body, copy_len); prompt[copy_len] = '\0'; }
        else { status = create_error_response(response, ROUTE_ERROR_MEMORY, 500); goto cleanup; }
    }

    /* Check cache first (semantic + exact) */
    char cache_key[512];
    snprintf(cache_key, sizeof(cache_key), "ai:%s:%s", model_name ? model_name : "default", prompt);
    char cached_response[INITIAL_AI_BUF_SIZE];
    if (cache_get(cache_key, cached_response, sizeof(cached_response)) == 0) {
        status = create_http_response(response, cached_response, strlen(cached_response), "application/json", 200, "OK");
        goto cleanup;
    }

    /* Semantic cache — match similar prompts */
    if (cache_semantic_get(prompt, model_name ? model_name : "default", cached_response, sizeof(cached_response), 0.85) == 0) {
        status = create_http_response(response, cached_response, strlen(cached_response), "application/json", 200, "OK");
        goto cleanup;
    }

    size_t prompt_len = strlen(prompt);
    if (prompt_len > MAX_LOG_PREVIEW) printf("[ROUTER] Received prompt (truncated): %.100s... [Length: %zu]\n", prompt, prompt_len);
    else printf("[ROUTER] Received prompt: %s\n", prompt);

    char *ai_response = malloc(INITIAL_AI_BUF_SIZE);
    if (!ai_response) { status = create_error_response(response, ROUTE_ERROR_MEMORY, 500); goto cleanup; }
    memset(ai_response, 0, INITIAL_AI_BUF_SIZE);

    char actual_model[128];
    memset(actual_model, 0, sizeof(actual_model));

    int route_result = prompt_router_route(prompt, model_name, ai_response, INITIAL_AI_BUF_SIZE, actual_model, sizeof(actual_model));
    uint64_t latency_us = (get_current_time_ns() - start_ns) / 1000;

    const char *used_model = actual_model[0] ? actual_model : (model_name ? model_name : "default");

    if (route_result == 0) {
        char *safe_ai_response = json_escape_str(ai_response);
        if (safe_ai_response) {
            size_t response_len = strlen(safe_ai_response) + strlen(used_model) + 128;
            char *json_output = malloc(response_len);
            if (json_output) {
                snprintf(json_output, response_len, "{\"response\": \"%s\", \"model\": \"%s\", \"status\": \"success\", \"latency_us\": %lu}",
                         safe_ai_response, used_model, (unsigned long)latency_us);
                status = create_http_response(response, json_output, strlen(json_output), "application/json", 200, "OK");
                cache_set(cache_key, json_output, strlen(json_output), 300);
                cache_semantic_set(prompt, used_model, json_output, strlen(json_output), 300);
                free(json_output);
            } else status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
            free(safe_ai_response);
        } else status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    } else {
        const char *error_msg = "AI Router Error: Failed to process request";
        status = create_http_response(response, error_msg, strlen(error_msg), "application/json", 502, "Bad Gateway");
    }

    free(ai_response);

cleanup:
    free(prompt); free(model_name);
    return status;
}

int handle_stats_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)request;
    if (!server || !response) return -1;
    ServerMetrics metrics;
    obs_get_metrics(&metrics);
    size_t stats_len = 1024;
    char *stats_json = malloc(stats_len);
    if (!stats_json) return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    int written = snprintf(stats_json, stats_len,
        "{"
        "\"requests\": %lu, \"responses\": %lu, "
        "\"active_connections\": %d, "
        "\"total_errors\": %lu, "
        "\"total_ai_calls\": %lu, "
        "\"uptime_seconds\": %.0f, "
        "\"bytes_sent\": %lu, \"bytes_received\": %lu, "
        "\"timestamp\": %ld"
        "}",
        (unsigned long)metrics.total_requests, (unsigned long)metrics.total_responses,
        server->active_connections,
        (unsigned long)metrics.total_errors, (unsigned long)metrics.total_ai_calls,
        metrics.uptime_seconds,
        (unsigned long)metrics.bytes_sent, (unsigned long)metrics.bytes_received,
        (long)time(NULL));
    if (written < 0 || (size_t)written >= stats_len) { free(stats_json); return create_error_response(response, ROUTE_ERROR_INTERNAL, 500); }
    int result = create_http_response(response, stats_json, strlen(stats_json), "application/json", 200, "OK");
    free(stats_json);
    return result;
}

int handle_health_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; (void)request;
    if (!response) return -1;
    size_t health_len = 256;
    char *health_json = malloc(health_len);
    if (!health_json) return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    int written = snprintf(health_json, health_len,
        "{\"status\": \"ok\", \"timestamp\": %ld, \"server\": \"AIONIC/1.0\", \"version\": \"1.0.0\"}",
        (long)time(NULL));
    if (written < 0 || (size_t)written >= health_len) { free(health_json); return create_error_response(response, ROUTE_ERROR_INTERNAL, 500); }
    int result = create_http_response(response, health_json, strlen(health_json), "application/json", 200, "OK");
    free(health_json);
    return result;
}

int handle_root_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; (void)request;
    if (!response) return -1;
    return copy_route_response(&cached_root_response, response);
}

int handle_metrics_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)request;
    if (!server || !response) return -1;
    ServerMetrics m;
    obs_get_metrics(&m);
    char buf[4096];
    int n = snprintf(buf, sizeof(buf),
        "# HELP aionic_requests_total Total HTTP requests\n"
        "# TYPE aionic_requests_total counter\n"
        "aionic_requests_total %lu\n"
        "# HELP aionic_responses_total Total HTTP responses\n"
        "# TYPE aionic_responses_total counter\n"
        "aionic_responses_total %lu\n"
        "# HELP aionic_active_connections Current active connections\n"
        "# TYPE aionic_active_connections gauge\n"
        "aionic_active_connections %lu\n"
        "# HELP aionic_errors_total Total errors\n"
        "# TYPE aionic_errors_total counter\n"
        "aionic_errors_total %lu\n"
        "# HELP aionic_ai_calls_total Total AI API calls\n"
        "# TYPE aionic_ai_calls_total counter\n"
        "aionic_ai_calls_total %lu\n"
        "# HELP aionic_uptime_seconds Server uptime\n"
        "# TYPE aionic_uptime_seconds gauge\n"
        "aionic_uptime_seconds %.0f\n",
        (unsigned long)m.total_requests, (unsigned long)m.total_responses,
        m.active_connections, (unsigned long)m.total_errors,
        (unsigned long)m.total_ai_calls, m.uptime_seconds);
    return create_http_response(response, buf, (size_t)n, "text/plain; charset=utf-8", 200, "OK");
}

int handle_providers_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; (void)request;
    if (!response) return -1;
    ProviderStats stats[64];
    int count = 0;
    obs_get_all_provider_stats(stats, &count, 64);
    size_t json_size = 256 + count * 256;
    char *json = malloc(json_size);
    if (!json) return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    char *ptr = json;
    ptr += sprintf(ptr, "{\"providers\": [");
    for (int i = 0; i < count; i++) {
        if (i > 0) ptr += sprintf(ptr, ",");
        double avg_latency = stats[i].total_calls > 0 ? (double)stats[i].total_latency_us / stats[i].total_calls / 1000.0 : 0;
        ptr += sprintf(ptr, "{\"name\":\"%s\",\"total_calls\":%lu,\"avg_latency_ms\":%.2f,\"errors\":%lu}",
                       stats[i].provider_name, (unsigned long)stats[i].total_calls,
                       avg_latency, (unsigned long)stats[i].errors);
    }
    ptr += sprintf(ptr, "],\"count\":%d}", count);
    int result = create_http_response(response, json, strlen(json), "application/json", 200, "OK");
    free(json);
    return result;
}

void router_init(void) { hash_table_init(&routes_table); init_cached_responses(); }

int handle_embeddings_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server;
    if (!response || !request || !request->body) {
        return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 400);
    }
    char model_buf[128] = {0};
    char input_buf[4096] = {0};
    if (json_get_value(request->body, "model", model_buf, sizeof(model_buf)) != 0) {
        return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 400);
    }
    if (json_get_value(request->body, "input", input_buf, sizeof(input_buf)) != 0) {
        return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 400);
    }
    const char *inputs[1] = { input_buf };
    char payload[8192];
    if (gateway_build_embeddings_payload(model_buf, inputs, 1, payload, sizeof(payload)) != 0) {
        return create_error_response(response, ROUTE_ERROR_INTERNAL, 500);
    }
    char ai_response[1048576] = {0};
    char actual_model[128] = {0};
    int result = prompt_router_route(input_buf, model_buf, ai_response, sizeof(ai_response), actual_model, sizeof(actual_model));
    if (result != 0) {
        return create_error_response(response, ROUTE_ERROR_NOT_FOUND, 502);
    }
    char resp_json[2097152];
    snprintf(resp_json, sizeof(resp_json),
        "{\"data\": [{\"embedding\": %s, \"index\": 0}], \"model\": \"%s\", \"usage\": {\"prompt_tokens\": %zu}}",
        ai_response, actual_model, strlen(input_buf) / 4);
    return create_http_response(response, resp_json, strlen(resp_json), "application/json", 200, "OK");
}

void router_cleanup(void) {
    free(cached_404_response.data);
    free(cached_root_response.data);
    hash_table_free(&routes_table);
}

void init_routes(void) {
    router_init();
    register_route("/v1/chat", HTTP_POST, handle_chat_request);
    register_route("/v1/chat/stream", HTTP_POST, handle_chat_request);
    register_route("/v1/embeddings", HTTP_POST, handle_embeddings_request);
    register_route("/v1/models", HTTP_GET, handle_models_request);
    register_route("/stats", HTTP_GET, handle_stats_request);
    register_route("/health", HTTP_GET, handle_health_request);
    register_route("/metrics", HTTP_GET, handle_metrics_request);
    register_route("/v1/providers", HTTP_GET, handle_providers_request);
    register_route("/", HTTP_GET, handle_root_request);
}

int handle_models_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; (void)request;
    if (!response) return -1;
    char **model_names = NULL;
    int model_count = 0;
    if (prompt_router_get_models(&model_names, &model_count) != 0) return create_error_response(response, ROUTE_ERROR_INTERNAL, 500);
    size_t json_size = 256 + (model_count * 128);
    char *json = malloc(json_size);
    if (!json) { for (int i = 0; i < model_count; i++) free(model_names[i]); free(model_names); return create_error_response(response, ROUTE_ERROR_MEMORY, 500); }
    char *ptr = json;
    ptr += sprintf(ptr, "{\"models\": [");
    for (int i = 0; i < model_count; i++) {
        if (i > 0) ptr += sprintf(ptr, ", ");
        ptr += sprintf(ptr, "\"%s\"", model_names[i]);
        free(model_names[i]);
    }
    free(model_names);
    ptr += sprintf(ptr, "], \"count\": %d}", model_count);
    int result = create_http_response(response, json, strlen(json), "application/json", 200, "OK");
    free(json);
    return result;
}
