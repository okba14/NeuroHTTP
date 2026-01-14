#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ===== External Library for Networking (HTTPS) =====
#include <curl/curl.h>

// ===== Project Headers =====
#include "prompt_router.h"
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"

// AI model structure definition
typedef struct {
    char *name;
    char *api_endpoint;
    int max_tokens;
    float temperature;
    int is_available;
    pthread_mutex_t mutex;
} AIModel;

// Prompt router structure definition
typedef struct {
    AIModel *models;
    int model_count;
    int model_capacity;
    pthread_mutex_t mutex;
    char *default_model;
} PromptRouter;

static PromptRouter global_router;

// === Helper: Structure to hold CURL response data ===
struct MemoryStruct {
    char *memory;
    size_t size;
};

// === Helper: CURL Write Callback ===

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        // Out of memory!
        printf("[AI_ROUTER] Error: Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// === Helper: Build JSON Payload (OpenAI Format) ===
static char* build_json_payload(const char *model_name, const char *prompt, float temp) {

    size_t prompt_len = strlen(prompt);
    size_t len = prompt_len + 256; 

    char *json = malloc(len);
    if (!json) return NULL;

    // Constructing JSON: {"model": "...", "messages": [{"role": "user", "content": "..."}], "temperature": ...}
    snprintf(json, len, 
        "{\"model\": \"%s\", \"messages\": [{\"role\": \"user\", \"content\": \"%s\"}], \"temperature\": %.1f}", 
        model_name, prompt, temp);
    
    return json;
}

// Function to parse AI model response (Adapted for OpenAI/Groq format)
static int parse_ai_response(const char *raw_response, char *output, size_t output_size) {
    if (!raw_response || !output || output_size == 0) {
        return -1;
    }
    

    char *content_start = strstr(raw_response, "\"content\":");
    
    if (!content_start) {

        content_start = strstr(raw_response, "\"response\":");
        if (!content_start) {
            return -1;
        }
    }
    
    content_start += strlen(content_start[0] == 'r' ? "\"response\":" : "\"content\":");
    

    while (*content_start && (*content_start == ' ' || *content_start == ':')) {
        content_start++;
    }
    
    if (*content_start == '"') {
        content_start++; 
        

        size_t i = 0;
        while (*content_start && *content_start != '"' && i < output_size - 1) {

            if (*content_start == '\\' && *(content_start + 1) == '"') {
                output[i++] = '"';
                content_start += 2;
            } else {
                output[i++] = *content_start++;
            }
        }
        output[i] = '\0';
        return 0;
    }
    
    return -1;
}

// Function to send request to AI model (REAL IMPLEMENTATION)
static int send_to_model(AIModel *model, const char *prompt, char *response, size_t response_size) {
    if (!model || !prompt || !response || response_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&model->mutex);
    
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1); 
    chunk.size = 0;

    curl = curl_easy_init();
    if(curl) {
        // Build JSON payload
        char *json_payload = build_json_payload(model->name, prompt, model->temperature);
        if (!json_payload) {
            pthread_mutex_unlock(&model->mutex);
            free(chunk.memory);
            return -1;
        }

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        // Handle API Key (Check environment variable)

        char *api_key = getenv("OPENAI_API_KEY");
        if (api_key) {
            char auth_header[256];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
            headers = curl_slist_append(headers, auth_header);
        } else {
            log_message("AI_ROUTER", "Warning: OPENAI_API_KEY environment variable not set.");
        }

        // Set CURL options
        curl_easy_setopt(curl, CURLOPT_URL, model->api_endpoint);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        
        // Disable SSL verification for development purposes 
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); 
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Perform request
        char log_msg[512];
        snprintf(log_msg, sizeof(log_msg), "Sending real request to model %s at %s", model->name, model->api_endpoint);
        log_message("AI_ROUTER", log_msg);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            snprintf(response, response_size, "{\"error\": \"curl_easy_perform() failed: %s\"}", curl_easy_strerror(res));
        } else {
            // Parse the received JSON
            if (parse_ai_response(chunk.memory, response, response_size) != 0) {

                strncpy(response, chunk.memory, response_size);
                response[response_size - 1] = '\0';
            }
        }

        // Cleanup resources
        free(chunk.memory);
        free(json_payload);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        strncpy(response, "{\"error\": \"Failed to initialize CURL\"}", response_size);
    }
    
    pthread_mutex_unlock(&model->mutex);
    return 0;
}

// Function to route prompts using optimized functions
int route_prompt_optimized(const char *prompt, char *response, size_t response_size, const char *model_name) {

    uint32_t prompt_hash = crc32_asm(prompt, strlen(prompt));
    
    // Use optimized memcpy for copying data
    char *prompt_copy = malloc(strlen(prompt) + 1);
    if (prompt_copy) {
        memcpy_asm(prompt_copy, prompt, strlen(prompt) + 1);
        

        // ...
        
        free(prompt_copy);
    }
    

    snprintf(response, response_size, 
             "{\"response\": \"Processed with optimized functions (hash: %u)\", \"model\": \"%s\"}", 
             prompt_hash, model_name);
    
    return 0;
}

// Initialize prompt router 
int prompt_router_init() {

    curl_global_init(CURL_GLOBAL_ALL);

    global_router.model_capacity = 8;
    global_router.models = calloc(global_router.model_capacity, sizeof(AIModel));
    if (!global_router.models) {
        return -1;
    }
    
    global_router.model_count = 0;
    global_router.default_model = NULL;
    
    if (pthread_mutex_init(&global_router.mutex, NULL) != 0) {
        free(global_router.models);
        return -1;
    }
    
    // === UPDATED GROQ MODELS (2025 Compatible) ===
    // Endpoint: https://api.groq.com/openai/v1/chat/completions
    
    // 1. Llama 3.3 70B Versatile 
    prompt_router_add_model("llama-3.3-70b-versatile", "https://api.groq.com/openai/v1/chat/completions", 8192, 0.7);
    
    // 2. Llama 3.1 8B Instant 
    prompt_router_add_model("llama-3.1-8b-instant", "https://api.groq.com/openai/v1/chat/completions", 8192, 0.7);
    
    // 3. Gemma 2 9B IT 
    prompt_router_add_model("gemma2-9b-it", "https://api.groq.com/openai/v1/chat/completions", 8192, 0.7);


    global_router.default_model = strdup("llama-3.3-70b-versatile");
    
    log_message("AI_ROUTER", "Prompt router initialized with updated GROQ support");
    return 0;
}

// Add AI model
int prompt_router_add_model(const char *name, const char *api_endpoint, int max_tokens, float temperature) {
    if (!name || !api_endpoint) {
        return -1;
    }
    
    pthread_mutex_lock(&global_router.mutex);
    
    if (global_router.model_count >= global_router.model_capacity) {
        // Expand model capacity
        int new_capacity = global_router.model_capacity * 2;
        AIModel *new_models = realloc(global_router.models, 
                                     sizeof(AIModel) * new_capacity);
        if (!new_models) {
            pthread_mutex_unlock(&global_router.mutex);
            return -1;
        }
        
        global_router.models = new_models;
        global_router.model_capacity = new_capacity;
    }
    
    // Initialize new model
    AIModel *model = &global_router.models[global_router.model_count];
    model->name = strdup(name);
    model->api_endpoint = strdup(api_endpoint);
    model->max_tokens = max_tokens;
    model->temperature = temperature;
    model->is_available = 1;
    
    if (pthread_mutex_init(&model->mutex, NULL) != 0) {
        free(model->name);
        free(model->api_endpoint);
        pthread_mutex_unlock(&global_router.mutex);
        return -1;
    }
    
    global_router.model_count++;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "AI model added: %s", name);
    log_message("AI_ROUTER", log_msg);
    
    pthread_mutex_unlock(&global_router.mutex);
    return 0;
}

// Remove AI model
int prompt_router_remove_model(const char *name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&global_router.mutex);
    
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            AIModel *model = &global_router.models[i];
            
            // Free resources
            pthread_mutex_destroy(&model->mutex);
            free(model->name);
            free(model->api_endpoint);
            
            // Remove model from list
            for (int j = i; j < global_router.model_count - 1; j++) {
                global_router.models[j] = global_router.models[j + 1];
            }
            
            global_router.model_count--;
            
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "AI model removed: %s", name);
            log_message("AI_ROUTER", log_msg);
            
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

// Set default model
int prompt_router_set_default_model(const char *name) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&global_router.mutex);
    
    // Check if model exists
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            if (global_router.default_model) {
                free(global_router.default_model);
            }
            
            global_router.default_model = strdup(name);
            
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Default AI model set: %s", name);
            log_message("AI_ROUTER", log_msg);
            
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

// Route prompt to AI model
int prompt_router_route(const char *prompt, const char *model_name, char *response, size_t response_size) {
    if (!prompt || !response || response_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&global_router.mutex);
    
    // Determine target model
    AIModel *target_model = NULL;
    
    if (model_name) {
        // Search for specified model
        for (int i = 0; i < global_router.model_count; i++) {
            if (strcmp(global_router.models[i].name, model_name) == 0) {
                target_model = &global_router.models[i];
                break;
            }
        }
    } else if (global_router.default_model) {
        // Use default model
        for (int i = 0; i < global_router.model_count; i++) {
            if (strcmp(global_router.models[i].name, global_router.default_model) == 0) {
                target_model = &global_router.models[i];
                break;
            }
        }
    }
    
    if (!target_model) {
        pthread_mutex_unlock(&global_router.mutex);
        return -1;
    }
    
    // Send request to model
    int result = send_to_model(target_model, prompt, response, response_size);
    
    pthread_mutex_unlock(&global_router.mutex);
    return result;
}

// Get list of available models
int prompt_router_get_models(char ***model_names, int *count) {
    pthread_mutex_lock(&global_router.mutex);
    
    *count = global_router.model_count;
    *model_names = malloc(sizeof(char *) * (*count));
    
    for (int i = 0; i < *count; i++) {
        (*model_names)[i] = strdup(global_router.models[i].name);
    }
    
    pthread_mutex_unlock(&global_router.mutex);
    return 0;
}

// Set model availability
int prompt_router_set_model_availability(const char *name, int is_available) {
    if (!name) {
        return -1;
    }
    
    pthread_mutex_lock(&global_router.mutex);
    
    for (int i = 0; i < global_router.model_count; i++) {
        if (strcmp(global_router.models[i].name, name) == 0) {
            global_router.models[i].is_available = is_available;
            
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "AI model %s availability: %s", 
                    name, is_available ? "available" : "unavailable");
            log_message("AI_ROUTER", log_msg);
            
            pthread_mutex_unlock(&global_router.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_router.mutex);
    return -1;
}

// Clean up prompt router
void prompt_router_cleanup() {
    pthread_mutex_lock(&global_router.mutex);
    
    // Free model resources
    for (int i = 0; i < global_router.model_count; i++) {
        pthread_mutex_destroy(&global_router.models[i].mutex);
        free(global_router.models[i].name);
        free(global_router.models[i].api_endpoint);
    }
    
    free(global_router.models);
    if (global_router.default_model) {
        free(global_router.default_model);
    }
    
    pthread_mutex_unlock(&global_router.mutex);
    pthread_mutex_destroy(&global_router.mutex);
    
    // Cleanup CURL globally
    curl_global_cleanup();
    
    log_message("AI_ROUTER", "Prompt router cleaned up");
}
