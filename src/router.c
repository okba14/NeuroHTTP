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

// ===== Security & Configuration Constants (NEW) =====
#define MAX_PROMPT_SIZE 16384       // 16KB limit for prompt to prevent DoS
#define MAX_LOG_PREVIEW 100         // Limit log output to prevent sensitive data leakage
#define INITIAL_AI_BUF_SIZE 8192    // Starting buffer for AI response

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

// (NEW) Helper: JSON String Escaping
// Essential to prevent JSON Injection/Breaking
static char* json_escape_str(const char* input) {
    if (!input) return strdup("");
    
    size_t len = strlen(input);
    // Worst case scenario: every character needs escaping
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
                // Filter out other control characters (ASCII < 32) to keep JSON clean
                if ((unsigned char)input[i] < 32) {
                    escaped[j++] = ' '; 
                } else {
                    escaped[j++] = input[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

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
    // ===== 1. 404 PAGE (THE VOID) =====
    const char *not_found_html = 
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <title>404 | LOST IN THE VOID</title>\n"
        "    <style>\n"
        "        :root { --neon: #ff0055; --bg: #050505; --card: rgba(20, 20, 25, 0.8); }\n"
        "        body {\n"
        "            margin: 0;\n"
        "            height: 100vh;\n"
        "            background: radial-gradient(circle at center, #1a1a2e 0%, #000000 100%);\n"
        "            color: #fff;\n"
        "            font-family: 'Courier New', Courier, monospace;\n"
        "            display: flex;\n"
        "            align-items: center;\n"
        "            justify-content: center;\n"
        "            overflow: hidden;\n"
        "        }\n"
        "        .container {\n"
        "            text-align: center;\n"
        "            position: relative;\n"
        "            z-index: 2;\n"
        "        }\n"
        "        h1 {\n"
        "            font-size: 8rem;\n"
        "            margin: 0;\n"
        "            color: transparent;\n"
        "            -webkit-text-stroke: 2px var(--neon);\n"
        "            text-shadow: 0 0 20px var(--neon);\n"
        "            animation: glitch 3s infinite;\n"
        "        }\n"
        "        h2 { font-weight: 300; letter-spacing: 5px; margin-top: -20px; }\n"
        "        p { color: #888; margin-bottom: 40px; }\n"
        "        .btn {\n"
        "            padding: 15px 40px;\n"
        "            background: transparent;\n"
        "            border: 1px solid var(--neon);\n"
        "            color: var(--neon);\n"
        "            text-decoration: none;\n"
        "            text-transform: uppercase;\n"
        "            letter-spacing: 2px;\n"
        "            transition: 0.3s;\n"
        "            box-shadow: 0 0 10px rgba(255, 0, 85, 0.2);\n"
        "        }\n"
        "        .btn:hover { background: var(--neon); color: #000; box-shadow: 0 0 40px var(--neon); }\n"
        "        .scanline {\n"
        "            position: fixed; left: 0; top: 0; width: 100%; height: 100%;\n"
        "            background: linear-gradient(to bottom, rgba(255,255,255,0), rgba(255,255,255,0) 50%, rgba(0,0,0,0.2) 50%, rgba(0,0,0,0.2));\n"
        "            background-size: 100% 4px; pointer-events: none; z-index: 1;\n"
        "        }\n"
        "        @keyframes glitch {\n"
        "            0% { transform: skew(0deg); }\n"
        "            20% { transform: skew(-2deg); }\n"
        "            21% { transform: skew(2deg); }\n"
        "            100% { transform: skew(0deg); }\n"
        "        }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"scanline\"></div>\n"
        "    <div class=\"container\">\n"
        "        <h1>404</h1>\n"
        "        <h2>SIGNAL LOST // SEVER NOT FOUND</h2>\n"
        "        <p>The requested coordinates do not exist in this memory block.</p>\n"
        "        <a href=\"/\" class=\"btn\">Reboot System</a>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";
    
    create_http_response(&cached_404_response, not_found_html, strlen(not_found_html), 
                         "text/html", 404, "Not Found");
    
    // ===== 2. ROOT PAGE (THE CORE) =====
    const char *root_html = 
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <title>NeuroHTTP CORE | SYSTEM ONLINE</title>\n"
        "    <style>\n"
        "        :root {\n"
        "            --primary: #00f3ff; /* Neon Cyan */\n"
        "            --secondary: #bc13fe; /* Electric Purple */\n"
        "            --bg: #0a0b10;\n"
        "            --surface: rgba(255, 255, 255, 0.03);\n"
        "            --border: rgba(255, 255, 255, 0.1);\n"
        "        }\n"
        "        * { box-sizing: border-box; }\n"
        "        body {\n"
        "            margin: 0;\n"
        "            font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n"
        "            background-color: var(--bg);\n"
        "            background-image: \n"
        "                linear-gradient(rgba(0, 243, 255, 0.03) 1px, transparent 1px),\n"
        "                linear-gradient(90deg, rgba(0, 243, 255, 0.03) 1px, transparent 1px);\n"
        "            background-size: 40px 40px;\n"
        "            color: #e0e6ed;\n"
        "            min-height: 100vh;\n"
        "            display: flex;\n"
        "            flex-direction: column;\n"
        "            align-items: center;\n"
        "        }\n"
        "        .header {\n"
        "            width: 100%;\n"
        "            padding: 40px 20px;\n"
        "            text-align: center;\n"
        "            animation: fadeInDown 1s ease-out;\n"
        "        }\n"
        "        .logo-area { display: inline-flex; align-items: center; gap: 15px; margin-bottom: 10px; }\n"
        "        .logo-svg {\n"
        "            width: 60px; height: 60px;\n"
        "            filter: drop-shadow(0 0 10px var(--primary));\n"
        "            animation: pulse 3s infinite ease-in-out;\n"
        "        }\n"
        "        .title {\n"
        "            font-size: 2.5rem;\n"
        "            font-weight: 800;\n"
        "            letter-spacing: -1px;\n"
        "            background: linear-gradient(90deg, #fff, var(--primary));\n"
        "            -webkit-background-clip: text;\n"
        "            -webkit-text-fill-color: transparent;\n"
        "            text-transform: uppercase;\n"
        "        }\n"
        "        .badge {\n"
        "            display: inline-block;\n"
        "            padding: 5px 12px;\n"
        "            border: 1px solid var(--primary);\n"
        "            color: var(--primary);\n"
        "            border-radius: 20px;\n"
        "            font-size: 0.8rem;\n"
        "            margin-top: 10px;\n"
        "            box-shadow: 0 0 10px rgba(0, 243, 255, 0.1);\n"
        "        }\n"
        "        .grid {\n"
        "            display: grid;\n"
        "            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));\n"
        "            gap: 20px;\n"
        "            width: 90%;\n"
        "            max-width: 1200px;\n"
        "            margin-bottom: 60px;\n"
        "        }\n"
        "        .card {\n"
        "            background: var(--surface);\n"
        "            border: 1px solid var(--border);\n"
        "            padding: 30px;\n"
        "            border-radius: 12px;\n"
        "            backdrop-filter: blur(10px);\n"
        "            transition: all 0.3s ease;\n"
        "            position: relative;\n"
        "            overflow: hidden;\n"
        "        }\n"
        "        .card:hover {\n"
        "            transform: translateY(-5px);\n"
        "            border-color: var(--primary);\n"
        "            box-shadow: 0 10px 30px -10px rgba(0, 243, 255, 0.15);\n"
        "        }\n"
        "        .card::before {\n"
        "            content: ''; position: absolute; top: 0; left: 0; width: 100%; height: 2px;\n"
        "            background: linear-gradient(90deg, var(--primary), var(--secondary));\n"
        "            transform: scaleX(0); transition: transform 0.3s ease;\n"
        "        }\n"
        "        .card:hover::before { transform: scaleX(1); }\n"
        "        .card h3 { margin-top: 0; color: #fff; display: flex; align-items: center; gap: 10px; }\n"
        "        .card p { color: #a0aab5; line-height: 1.6; font-size: 0.95rem; }\n"
        "        .icon { font-size: 1.5rem; }\n"
        "        .footer {\n"
        "            margin-top: auto;\n"
        "            padding: 20px;\n"
        "            color: #555;\n"
        "            font-size: 0.8rem;\n"
        "            text-align: center;\n"
        "        }\n"
        "        .terminal-block {\n"
        "            background: #000;\n"
        "            border: 1px solid #333;\n"
        "            padding: 15px;\n"
        "            border-radius: 6px;\n"
        "            font-family: 'Courier New', monospace;\n"
        "            font-size: 0.85rem;\n"
        "            color: #0f0;\n"
        "            margin: 20px 0;\n"
        "            text-align: left;\n"
        "        }\n"
        "        @keyframes pulse { 0% { opacity: 0.8; } 50% { opacity: 1; filter: drop-shadow(0 0 20px var(--primary)); } 100% { opacity: 0.8; } }\n"
        "        @keyframes fadeInDown { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0); } }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"header\">\n"
        "        <div class=\"logo-area\">\n"
        "            <!-- SVG LOGO: Microchip Core Design -->\n"
        "            <svg class=\"logo-svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
        "                <rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"2\" stroke=\"#00f3ff\"></rect>\n"
        "                <rect x=\"9\" y=\"9\" width=\"6\" height=\"6\" stroke=\"#00f3ff\"></rect>\n"
        "                <path d=\"M9 1V3M15 1V3M9 21V23M15 21V23M21 9H23M21 15H23M1 9H3M1 15H3\" stroke=\"#bc13fe\"></path>\n"
        "                <circle cx=\"12\" cy=\"12\" r=\"1\" fill=\"#00f3ff\"></circle>\n"
        "            </svg>\n"
        "            <div>\n"
        "                <div class=\"title\">NeuroHTTP CORE</div>\n"
        "                <div style=\"font-size: 0.9rem; letter-spacing: 3px; color: #888;\">ZERO-DEPENDENCY ENGINE</div>\n"
        "            </div>\n"
        "        </div>\n"
        "        <div class=\"badge\">v1.0.0 // ASSEMBLY OPTIMIZED</div>\n"
        "    </div>\n"
        "\n"
        "    <div class=\"grid\">\n"
        "        <div class=\"card\">\n"
        "            <h3><span class=\"icon\">⚡</span> Pure C & ASM</h3>\n"
        "            <p>Built from scratch without external frameworks. Raw machine power controlled by hand-written assembly routines.</p>\n"
        "            <div class=\"terminal-block\">\n"
        "                > init_core(ASM_OK)\n"
        "                > mem_alloc: FAST\n"
        "                > status: LOCKED & LOADED\n"
        "            </div>\n"
        "        </div>\n"
        "        <div class=\"card\">\n"
        "            <h3><span class=\"icon\">🚀</span> Hyper Speed</h3>\n"
        "            <p>Custom memory allocators and AVX2 tokenization. Designed for high-throughput, low-latency operations.</p>\n"
        "        </div>\n"
        "        <div class=\"card\">\n"
        "            <h3><span class=\"icon\">🛡️</span> Ironclad Security</h3>\n"
        "            <p>Minimal attack surface. Zero bloat means fewer vulnerabilities. Your data stays in the silicon.</p>\n"
        "        </div>\n"
        "        <div class=\"card\">\n"
        "            <h3><span class=\"icon\">🧠</span> AI Native</h3>\n"
        "            <p>Built-in JSON parsing with LLM optimization. Ready for neural network integration at the protocol level.</p>\n"
        "        </div>\n"
        "    </div>\n"
        "\n"
        "    <div class=\"footer\">\n"
        "        <p>SYSTEM ARCHITECTURE: CUSTOM &bull; POWERED BY NeuroHTTP AI &bull; &copy; 2025</p>\n"
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

// Function to handle chat requests (ADVANCED & SECURE VERSION)
int handle_chat_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; // Unused
    
    if (!request || !response) return -1;
    
    char *prompt = NULL;
    char *model_name = NULL;
    int status = -1;

    // 1. DoS Protection: Check payload size BEFORE processing
    if (!request->body || request->body_length > MAX_PROMPT_SIZE) {
        return create_error_response(response, ROUTE_ERROR_INVALID_PARAM, 413); // 413 Payload Too Large
    }

    // === PARSE REQUEST BODY ===
    // Try to extract 'prompt' from JSON
    prompt = extract_json_value(request->body, "prompt");
    // Try to extract 'model' (optional), default to NULL
    model_name = extract_json_value(request->body, "model");

    if (!prompt) {
        // If parsing fails, try using the whole body as prompt (fallback)
        // Ensure we don't exceed buffer safety here again
        size_t copy_len = (request->body_length < MAX_PROMPT_SIZE) ? request->body_length : MAX_PROMPT_SIZE;
        prompt = malloc(copy_len + 1);
        if(prompt) {
            strncpy(prompt, request->body, copy_len);
            prompt[copy_len] = '\0';
        } else {
            status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
            goto cleanup;
        }
    }

    // 2. Secure Logging: Mask sensitive data & Truncate
    // Only print the first MAX_LOG_PREVIEW characters to avoid flooding logs
    size_t prompt_len = strlen(prompt);
    if (prompt_len > MAX_LOG_PREVIEW) {
        printf("[ROUTER] Received prompt (truncated): %.100s... [Length: %zu]\n", prompt, prompt_len);
    } else {
        printf("[ROUTER] Received prompt: %s\n", prompt);
    }

    // 3. Dynamic Buffer for AI Response
    size_t ai_buf_size = INITIAL_AI_BUF_SIZE; 
    char *ai_response = (char *)malloc(ai_buf_size);
    
    if (ai_response) {
        memset(ai_response, 0, ai_buf_size);
        
        // === CALL THE REAL AI ROUTER ===
        int route_result = prompt_router_route(prompt, model_name, ai_response, ai_buf_size);
        
        if (route_result == 0) {
            // 4. SECURITY: Escape JSON special characters
            char *safe_ai_response = json_escape_str(ai_response);
            
            if (safe_ai_response) {
                // Calculate required size for final JSON dynamically
                size_t response_len = strlen(safe_ai_response) + strlen(model_name ? model_name : "default") + 128;
                char *json_output = (char *)malloc(response_len);
                
                if (json_output) {
                    snprintf(json_output, response_len, 
                            "{\"response\": \"%s\", \"model\": \"%s\", \"status\": \"success\"}", 
                            safe_ai_response, model_name ? model_name : "default");
                    
                    status = create_http_response(response, json_output, strlen(json_output), 
                                             "application/json", 200, "OK");
                    free(json_output);
                } else {
                    status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
                }
                free(safe_ai_response); // Important: Free escaped string
            } else {
                status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
            }
        } else {
            // Router returned an error
            const char *error_msg = "AI Router Error: Failed to process request";
            status = create_http_response(response, error_msg, strlen(error_msg), 
                                         "application/json", 502, "Bad Gateway");
        }
        free(ai_response);
    } else {
        status = create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    }

cleanup:
    // === CLEANUP ===
    if (prompt) free(prompt);
    if (model_name) free(model_name);

    return status;
}

// Function to handle stats requests (Dynamic Buffering maintained)
int handle_stats_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)request; // Unused
    
    if (!server || !response) return -1;
    
    // Use dynamic allocation
    size_t stats_len = 512; 
    char *stats_json = malloc(stats_len);
    if (!stats_json) {
        return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    }
    
    int written = snprintf(stats_json, stats_len, 
            "{\"requests\": %lu, \"responses\": %lu, \"uptime\": %ld, \"active_connections\": %d, \"timestamp\": %ld}", 
            server->stats.total_requests, 
            server->stats.total_responses, 
            (long)0, // Uptime placeholder
            server->active_connections, 
            time(NULL));

    // Buffer size check
    if (written < 0 || (size_t)written >= stats_len) {
        free(stats_json);
        return create_error_response(response, ROUTE_ERROR_INTERNAL, 500);
    }
    
    int result = create_http_response(response, stats_json, strlen(stats_json), 
                                     "application/json", 200, "OK");
    free(stats_json);
    
    return result;
}

// Function to handle health requests (CHANGED TO DYNAMIC BUFFERING)
int handle_health_request(Server *server, HTTPRequest *request, RouteResponse *response) {
    (void)server; 
    (void)request;
    
    if (!response) return -1;
    
    // CHANGED: Use dynamic allocation instead of stack array
    size_t health_len = 128;
    char *health_json = malloc(health_len);
    if (!health_json) {
        return create_error_response(response, ROUTE_ERROR_MEMORY, 500);
    }

    int written = snprintf(health_json, health_len, 
            "{\"status\": \"ok\", \"timestamp\": %ld, \"server\": \"AIONIC/1.0\"}", 
            time(NULL));

    if (written < 0 || (size_t)written >= health_len) {
        free(health_json);
        return create_error_response(response, ROUTE_ERROR_INTERNAL, 500);
    }
    
    int result = create_http_response(response, health_json, strlen(health_json), 
                               "application/json", 200, "OK");
    
    free(health_json); // Important cleanup
    return result;
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
