#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>

// ===== Project Headers =====
#include "router.h"
#include "parser.h"
#include "stream.h"
#include "ai/prompt_router.h"
#include "asm_utils.h"
#include "server.h" 

// ===== Constants =====
#define MAX_CACHED_RESPONSES 16
#define MAX_MIDDLEWARE 8
#define MAX_ROUTE_PARAMS 8

// ===== Global Variables =====
static RouteHashTable routes_table = {0};  // Hash table for routes

// Cached responses for common paths
static RouteResponse cached_404_response = {0};
static RouteResponse cached_root_response = {0};

// Middleware functions
static MiddlewareFunc middlewares[MAX_MIDDLEWARE] = {NULL};
static int middleware_count = 0;

// Error messages
static const char* route_error_messages[] = {
    "No error",
    "Memory allocation failed",
    "Invalid parameter",
    "Route not found",
    "Internal server error"
};

// ===== Helper Functions =====

// Simple helper to extract a string from JSON body (Basic implementation)
// Assumes input is like {"key": "value"}
static char* extract_json_value(const char *json, const char *key) {
    if (!json || !key) return NULL;
    
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    char *key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;
    
    key_pos += strlen(search_key);
    while (*key_pos && (*key_pos == ' ' || *key_pos == ':')) {
        key_pos++;
    }
    
    if (*key_pos == '"') {
        key_pos++;
        char *end = strchr(key_pos, '"');
        if (end) {
            size_t len = end - key_pos;
            char *value = malloc(len + 1);
            if (value) {
                strncpy(value, key_pos, len);
                value[len] = '\0';
                return value;
            }
        }
    }
    return NULL;
}

// Helper function to check if a method matches
static int method_matches(HTTPMethod method1, HTTPMethod method2) {
    return method1 == method2;
}

// Check if a route path matches a request path with parameters
static int route_matches_with_params(const char *route_path, const char *request_path) {
    char route_copy[256];
    char request_copy[256];
    
    strncpy(route_copy, route_path, sizeof(route_copy) - 1);
    strncpy(request_copy, request_path, sizeof(request_copy) - 1);
    
    char *route_token = strtok(route_copy, "/");
    char *request_token = strtok(request_copy, "/");
    
    while (route_token != NULL && request_token != NULL) {
        if (route_token[0] != ':' && strcmp(route_token, request_token) != 0) {
            return 0;  // No match
        }
        
        route_token = strtok(NULL, "/");
        request_token = strtok(NULL, "/");
    }
    
    // Both should reach end
    return route_token == NULL && request_token == NULL;
}

// ===== Hash Table Functions =====

// Simple hash function for strings (djb2 algorithm)
static unsigned int hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash % HASH_TABLE_SIZE;
}

// Initialize the hash table
static void hash_table_init(RouteHashTable *table) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        table->buckets[i] = NULL;
    }
    pthread_rwlock_init(&table->lock, NULL);
}

// Add a route to the hash table
static int hash_table_add(RouteHashTable *table, Route *route) {
    unsigned int index = hash_string(route->path);
    
    pthread_rwlock_wrlock(&table->lock);
    
    // Add to the beginning of the bucket
    route->next = table->buckets[index];
    table->buckets[index] = route;
    
    pthread_rwlock_unlock(&table->lock);
    
    return 0;
}

// Find a route in the hash table
static Route *hash_table_find(RouteHashTable *table, const char *path, HTTPMethod method) {
    unsigned int index = hash_string(path);
    
    pthread_rwlock_rdlock(&table->lock);
    
    Route *current = table->buckets[index];
    while (current != NULL) {
        if (strcmp(current->path, path) == 0 && method_matches(current->method, method)) {
            pthread_rwlock_unlock(&table->lock);
            return current;
        }
        current = current->next;
    }
    
    pthread_rwlock_unlock(&table->lock);
    return NULL;
}

// Find a route with parameters in the hash table
static Route *hash_table_find_with_params(RouteHashTable *table, const char *path, HTTPMethod method) {
    // We need to check all routes for parameter matching
    pthread_rwlock_rdlock(&table->lock);
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        Route *current = table->buckets[i];
        while (current != NULL) {
            if (method_matches(current->method, method) && route_matches_with_params(current->path, path)) {
                pthread_rwlock_unlock(&table->lock);
                return current;
            }
            current = current->next;
        }
    }
    
    pthread_rwlock_unlock(&table->lock);
    return NULL;
}

// Free all routes in the hash table
static void hash_table_free(RouteHashTable *table) {
    pthread_rwlock_wrlock(&table->lock);
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        Route *current = table->buckets[i];
        while (current != NULL) {
            Route *next = current->next;
            free(current->path);
            free(current);
            current = next;
        }
        table->buckets[i] = NULL;
    }
    
    pthread_rwlock_unlock(&table->lock);
    pthread_rwlock_destroy(&table->lock);
}

// Helper function to create a complete HTTP response (optimized)
static int create_http_response(RouteResponse *response, const char *body, size_t body_length, 
                               const char *content_type, int status_code, const char *status_message) {
    if (!response || !body) return -1;
    
    // Calculate required buffer size
    time_t now;
    time(&now);
    struct tm *tm_info = gmtime(&now);
    char date_buf[128];
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    // Calculate headers size
    size_t headers_size = 128;  // Base size
    headers_size += strlen(date_buf);
    headers_size += strlen(content_type);
    headers_size += 32;  
    
    size_t total_size = headers_size + body_length;
    
    // Allocate response buffer
    response->data = malloc(total_size);
    if (!response->data) return -1;
    
    // Build response directly in memory
    char *ptr = response->data;
    
    // Add HTTP status line
    ptr += sprintf(ptr, "HTTP/1.1 %d %s\r\n", status_code, status_message);
    
    // Add headers
    ptr += sprintf(ptr, "Date: %s\r\n", date_buf);
    ptr += sprintf(ptr, "Server: AIONIC/1.0\r\n");
    ptr += sprintf(ptr, "Content-Type: %s\r\n", content_type);
    ptr += sprintf(ptr, "Content-Length: %zu\r\n", body_length);
    ptr += sprintf(ptr, "Connection: close\r\n\r\n");
    
    // Add body
    memcpy(ptr, body, body_length);
    
    // Set response properties
    response->length = (ptr - response->data) + body_length;
    response->status_code = status_code;
    response->status_message = (char *)status_message;
    response->is_streaming = 0;
    
    return 0;
}

// Create error response with consistent format
int create_error_response(RouteResponse *response, RouteError error, int status_code) {
    const char *error_message = route_error_messages[error];
    
    // Create JSON error response
    char *json_response = malloc(256);
    if (!json_response) return ROUTE_ERROR_MEMORY;
    
    snprintf(json_response, 256, 
            "{\"error\": \"%s\", \"code\": %d, \"timestamp\": %ld}", 
            error_message, error, time(NULL));
    
    // Create HTTP response
    int result = create_http_response(response, json_response, strlen(json_response), 
                                    "application/json", status_code, error_message);
    
    free(json_response);
    return result;
}

// Copy a response to another (for cached responses)
static int copy_route_response(const RouteResponse *source, RouteResponse *dest) {
    if (!source || !dest) return -1;
    
    // Allocate memory for the data
    dest->data = malloc(source->length);
    if (!dest->data) return -1;
    
    // Copy the data
    memcpy(dest->data, source->data, source->length);
    dest->length = source->length;
    dest->status_code = source->status_code;
    dest->status_message = source->status_message;
    dest->is_streaming = source->is_streaming;
    
    return 0;
}

// Initialize cached responses
static void init_cached_responses() {
    // Initialize 404 response
    const char *not_found_html = 
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <title>404 - Page Not Found | AIONIC Server</title>\n"
        "    <style>\n"
        "        body {\n"
        "            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
        "            line-height: 1.6;\n"
        "            color: #333;\n"
        "            max-width: 800px;\n"
        "            margin: 0 auto;\n"
        "            padding: 20px;\n"
        "            background-color: #f8f9fa;\n"
        "        }\n"
        "        .container {\n"
        "            background-color: white;\n"
        "            border-radius: 8px;\n"
        "            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);\n"
        "            padding: 30px;\n"
        "            text-align: center;\n"
        "        }\n"
        "        h1 {\n"
        "            color: #e74c3c;\n"
        "            margin-bottom: 20px;\n"
        "        }\n"
        "        p {\n"
        "            margin-bottom: 20px;\n"
        "        }\n"
        "        .home-link {\n"
        "            display: inline-block;\n"
        "            background-color: #3498db;\n"
        "            color: white;\n"
        "            padding: 10px 20px;\n"
        "            text-decoration: none;\n"
        "            border-radius: 4px;\n"
        "            transition: background-color 0.3s;\n"
        "        }\n"
        "        .home-link:hover {\n"
        "            background-color: #2980b9;\n"
        "        }\n"
        "        .footer {\n"
        "            margin-top: 30px;\n"
        "            font-size: 0.9em;\n"
        "            color: #7f8c8d;\n"
        "        }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <h1>404 - Page Not Found</h1>\n"
        "        <p>The page you are looking for might have been removed, had its name changed, or is temporarily unavailable.</p>\n"
        "        <p>Please check the URL for any typos or return to the homepage.</p>\n"
        "        <a href=\"/\" class=\"home-link\">Go to Homepage</a>\n"
        "        <div class=\"footer\">\n"
        "            <p>Powered by AIONIC AI Web Server</p>\n"
        "        </div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";
    
    create_http_response(&cached_404_response, not_found_html, strlen(not_found_html), 
                         "text/html", 404, "Not Found");
    
    // Initialize root response
    const char *root_html = 
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <title>Welcome to AIONIC AI Web Server</title>\n"
        "    <style>\n"
        "        body {\n"
        "            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
        "            line-height: 1.6;\n"
        "            color: #333;\n"
        "            max-width: 1000px;\n"
        "            margin: 0 auto;\n"
        "            padding: 20px;\n"
        "            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);\n"
        "            min-height: 100vh;\n"
        "        }\n"
        "        .container {\n"
        "            background-color: white;\n"
        "            border-radius: 12px;\n"
        "            box-shadow: 0 8px 30px rgba(0, 0, 0, 0.12);\n"
        "            padding: 40px;\n"
        "            text-align: center;\n"
        "            margin-top: 50px;\n"
        "        }\n"
        "        h1 {\n"
        "            color: #2c3e50;\n"
        "            margin-bottom: 20px;\n"
        "            font-size: 2.5em;\n"
        "        }\n"
        "        .subtitle {\n"
        "            color: #7f8c8d;\n"
        "            font-size: 1.2em;\n"
        "            margin-bottom: 30px;\n"
        "        }\n"
        "        .features {\n"
        "            display: flex;\n"
        "            justify-content: space-around;\n"
        "            margin: 40px 0;\n"
        "            flex-wrap: wrap;\n"
        "        }\n"
        "        .feature {\n"
        "            flex: 1;\n"
        "            min-width: 200px;\n"
        "            margin: 10px;\n"
        "            padding: 20px;\n"
        "            background-color: #f8f9fa;\n"
        "            border-radius: 8px;\n"
        "        }\n"
        "        .feature h3 {\n"
        "            color: #3498db;\n"
        "        }\n"
        "        .api-button {\n"
        "            display: inline-block;\n"
        "            background-color: #3498db;\n"
        "            color: white;\n"
        "            padding: 12px 24px;\n"
        "            text-decoration: none;\n"
        "            border-radius: 4px;\n"
        "            transition: background-color 0.3s;\n"
        "            margin: 10px;\n"
        "        }\n"
        "        .api-button:hover {\n"
        "            background-color: #2980b9;\n"
        "        }\n"
        "        .footer {\n"
        "            margin-top: 40px;\n"
        "            font-size: 0.9em;\n"
        "            color: #7f8c8d;\n"
        "        }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <h1>🚀 AIONIC AI Web Server</h1>\n"
        "        <p class=\"subtitle\">High-performance web server with AI capabilities</p>\n"
        "        \n"
        "        <div class=\"features\">\n"
        "            <div class=\"feature\">\n"
        "                <h3>⚡ High Performance</h3>\n"
        "                <p>Optimized for speed with multi-threading and efficient resource management</p>\n"
        "            </div>\n"
        "            <div class=\"feature\">\n"
        "                <h3>🧠 AI Integration</h3>\n"
        "                <p>Built-in AI prompt routing and processing capabilities</p>\n"
        "            </div>\n"
        "            <div class=\"feature\">\n"
        "                <h3>🔒 Security</h3>\n"
        "                <p>Advanced firewall and security features built-in</p>\n"
        "            </div>\n"
        "        </div>\n"
        "        \n"
        "        <div>\n"
        "            <a href=\"/health\" class=\"api-button\">Health Check</a>\n"
        "            <a href=\"/stats\" class=\"api-button\">Server Stats</a>\n"
        "        </div>\n"
        "        \n"
        "        <div class=\"footer\">\n"
        "            <p>Powered by AIONIC AI Web Server v1.0 | Built with C and Assembly</p>\n"
        "        </div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";
    
    create_http_response(&cached_root_response, root_html, strlen(root_html), 
                         "text/html", 200, "OK");
}

// ===== Core Routing Functions =====

// Register a new route with parameter support
int register_route(const char *path, HTTPMethod method, int (*handler)(Server *, HTTPRequest *, RouteResponse *)) {
    Route *route = malloc(sizeof(Route));
    if (!route) return -1;
    
    route->path = strdup(path);
    route->method = method;
    route->handler = handler;
    route->param_count = 0;
    route->next = NULL;
    
    // Parse path for parameters (e.g., /users/:id)
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    while (token != NULL && route->param_count < MAX_ROUTE_PARAMS) {
        if (token[0] == ':') {
            // This is a parameter
            strncpy(route->param_names[route->param_count], token + 1, 31);
            route->param_names[route->param_count][31] = '\0';
            route->param_count++;
        }
        token = strtok(NULL, "/");
    }
    free(path_copy);
    
    // Add to hash table
    return hash_table_add(&routes_table, route);
}

// Register a middleware function
int register_middleware(MiddlewareFunc middleware) {
    if (middleware_count >= MAX_MIDDLEWARE) {
        return -1;  
    }
    
    middlewares[middleware_count++] = middleware;
    return 0;
}

// Route a request to the appropriate handler
int route_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) {
        return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 400);
    }
    
    memset(response, 0, sizeof(RouteResponse));
    
    // Apply middleware in order
    for (int i = 0; i < middleware_count; i++) {
        int result = middlewares[i](request, response);
        if (result != 0) {
            // Middleware returned an error, stop processing
            return result;
        }
        
        // If middleware created a complete response, return it
        if (response->data != NULL) {
            return 0;
        }
    }
    
    // Check for cached responses first
    if (strcmp(request->path, "/") == 0 && method_matches(request->method, HTTP_GET)) {
        return copy_route_response(&cached_root_response, response);
    }
    
    // Look for exact match in hash table
    Route *route = hash_table_find(&routes_table, request->path, request->method);
    
    if (route) {
        return route->handler(server, request, response);
    }
    
    // If no exact match, try routes with parameters
    route = hash_table_find_with_params(&routes_table, request->path, request->method);
    
    if (route) {
        return route->handler(server, request, response);
    }
    
    // No matching route found - return cached 404 response
    return copy_route_response(&cached_404_response, response);
}

// Free route response resources
void free_route_response(RouteResponse *response) {
    if (!response) return;
    
    if (response->data) free(response->data);
    if (response->stream_data) free(response->stream_data);
    
    for (int i = 0; i < response->header_count; i++) {
        if (response->headers[i]) free(response->headers[i]);
    }
    
    memset(response, 0, sizeof(RouteResponse));
}

// ===== Route Handlers =====

// Function to handle chat requests (UPDATED TO USE REAL AI ROUTER)
int handle_chat_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; // Unused
    
    if (!request || !response) return -1;
    
    char *prompt = NULL;
    char *model_name = NULL;
    int status = -1;

    // === PARSE REQUEST BODY ===
    if (request->body && request->body_length > 0) {
        // Try to extract 'prompt' from JSON
        prompt = extract_json_value(request->body, "prompt");
        // Try to extract 'model' (optional), default to NULL
        model_name = extract_json_value(request->body, "model");

        if (!prompt) {
            // If parsing fails, try using the whole body as prompt (fallback)
            prompt = malloc(request->body_length + 1);
            if(prompt) {
                strncpy(prompt, request->body, request->body_length);
                prompt[request->body_length] = '\0';
            } else {
                return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
            }
        }

        printf("[ROUTER] Received prompt: %s\n", prompt);

        // Buffer for AI response
        size_t ai_buf_size = 8192;
        char *ai_response = (char *)malloc(ai_buf_size);
        
        if (ai_response) {
            // === CALL THE REAL AI ROUTER ===
            int route_result = prompt_router_route(prompt, model_name, ai_response, ai_buf_size);
            
            if (route_result == 0) {
                // Construct the JSON response for the client
                // We wrap the AI content in a JSON structure
                size_t response_len = strlen(ai_response) + 256;
                char *json_output = (char *)malloc(response_len);
                
                if (json_output) {
                    snprintf(json_output, response_len, 
                            "{\"response\": \"%s\", \"model\": \"%s\", \"status\": \"success\"}", 
                            ai_response, model_name ? model_name : "default");
                    
                    status = create_http_response(response, json_output, strlen(json_output), 
                                             "application/json", 200, "OK");
                    free(json_output);
                } else {
                    status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
                }
            } else {
                // Router returned an error
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "AI Router Error: Failed to process request");
                status = create_http_response(response, error_msg, strlen(error_msg), 
                                             "application/json", 502, "Bad Gateway");
            }
            free(ai_response);
        } else {
            status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
        }
    } else {
        status = create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 400);
    }

    // === CLEANUP ===
    if (prompt) free(prompt);
    if (model_name) free(model_name);

    return status;
}

// Function to handle stats requests
int handle_stats_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)request; // Unused
    
    if (!server || !response) return -1;
    
    char *stats_json = malloc(1024);
    if (!stats_json) {
        return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    }
    
    snprintf(stats_json, 1024, 
            "{\"requests\": %lu, \"responses\": %lu, \"uptime\": %ld, \"active_connections\": %d, \"timestamp\": %ld}", 
            server->stats.total_requests, 
            server->stats.total_responses, 
            (long)0, // Uptime placeholder
            server->active_connections, 
            time(NULL));
    
    int result = create_http_response(response, stats_json, strlen(stats_json), 
                                     "application/json", 200, "OK");
    free(stats_json);
    
    return result;
}

// Function to handle health requests
int handle_health_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; 
    (void)request;
    
    if (!response) return -1;
    
    const char *health_response = "{\"status\": \"ok\", \"timestamp\": %ld, \"server\": \"AIONIC/1.0\"}";
    char health_json[128];
    snprintf(health_json, sizeof(health_json), health_response, time(NULL));
    
    return create_http_response(response, health_json, strlen(health_json), 
                               "application/json", 200, "OK");
}

// Handle root path request
int handle_root_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server;
    (void)request;
    
    if (!response) return -1;
    
    return copy_route_response(&cached_root_response, response);
}

// ===== Initialization and Cleanup =====

// Initialize router system
void router_init(void) {
    // Initialize hash table
    hash_table_init(&routes_table);
    
    // Initialize cached responses
    init_cached_responses();
}

// Clean up router resources
void router_cleanup(void) {
    // Free cached responses
    if (cached_404_response.data) free(cached_404_response.data);
    if (cached_root_response.data) free(cached_root_response.data);
    
    // Free hash table
    hash_table_free(&routes_table);
}

// Function to initialize routes
void init_routes(void) {
    // Initialize router system
    router_init();
    
    // Register routes
    register_route("/v1/chat", HTTP_POST, handle_chat_request);
    register_route("/stats", HTTP_GET, handle_stats_request);
    register_route("/health", HTTP_GET, handle_health_request);
    register_route("/", HTTP_GET, handle_root_request);
}
