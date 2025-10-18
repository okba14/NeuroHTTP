#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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

// Function to parse AI model response
static int parse_ai_response(const char *raw_response, char *output, size_t output_size) {
    if (!raw_response || !output || output_size == 0) {
        return -1;
    }
    
    // Search for response text in JSON
    char *response_start = strstr(raw_response, "\"response\":");
    if (!response_start) {
        return -1;
    }
    
    response_start += strlen("\"response\":");
    
    // Skip spaces and colon
    while (*response_start && (*response_start == ' ' || *response_start == ':')) {
        response_start++;
    }
    
    if (*response_start == '"') {
        response_start++;
        char *response_end = strchr(response_start, '"');
        if (!response_end) {
            return -1;
        }
        
        size_t response_len = response_end - response_start;
        if (response_len >= output_size) {
            response_len = output_size - 1;
        }
        
        strncpy(output, response_start, response_len);
        output[response_len] = '\0';
        
        return 0;
    }
    
    return -1;
}

// Function to send request to AI model
static int send_to_model(AIModel *model, const char *prompt, char *response, size_t response_size) {
    if (!model || !prompt || !response || response_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&model->mutex);
    
    // Simulate sending request to AI model
    // In actual implementation, a real HTTP request would be made here
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "Sending prompt to model %s: %s", model->name, prompt);
    log_message("AI_ROUTER", log_msg);
    
    // Fake response - use parse_ai_response to avoid warning
    const char *fake_raw_response = "{\"response\": \"This is a simulated AI response\"}";
    parse_ai_response(fake_raw_response, response, response_size);
    
    pthread_mutex_unlock(&model->mutex);
    return 0;
}

// Function to route prompts using optimized functions
int route_prompt_optimized(const char *prompt, char *response, size_t response_size, const char *model_name) {
    // Use optimized CRC32 to create a hash of the prompt for caching/routing
    uint32_t prompt_hash = crc32_asm(prompt, strlen(prompt));
    
    // Use optimized memcpy for copying data
    char *prompt_copy = malloc(strlen(prompt) + 1);
    if (prompt_copy) {
        memcpy_asm(prompt_copy, prompt, strlen(prompt) + 1);
        
        // Process the prompt copy
        // ...
        
        free(prompt_copy);
    }
    
    // For now, just return a simple response
    snprintf(response, response_size, 
             "{\"response\": \"Processed with optimized functions (hash: %u)\", \"model\": \"%s\"}", 
             prompt_hash, model_name);
    
    return 0;
}

// Initialize prompt router
int prompt_router_init() {
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
    
    // Add default models
    prompt_router_add_model("gpt-3.5-turbo", "https://api.openai.com/v1/chat/completions", 2048, 0.7);
    prompt_router_add_model("claude-2", "https://api.anthropic.com/v1/messages", 4096, 0.5);
    prompt_router_add_model("llama-2", "http://localhost:8000/completion", 1024, 0.8);
    
    // Set default model
    global_router.default_model = strdup("gpt-3.5-turbo");
    
    log_message("AI_ROUTER", "Prompt router initialized");
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
    
    log_message("AI_ROUTER", "Prompt router cleaned up");
}
