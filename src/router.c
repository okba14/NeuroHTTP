#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// ===== Project Headers =====
#include "router.h"
#include "parser.h"
#include "stream.h"
#include "ai/prompt_router.h"
#include "asm_utils.h"


// Registered routes list with dynamic allocation
static Route *routes = NULL;
static int route_count = 0;
static int route_capacity = 0;

// Helper function to check if a method matches
static int method_matches(HTTPMethod method1, HTTPMethod method2) {
    return method1 == method2;
}

// Helper function to create a complete HTTP response
static int create_http_response(RouteResponse *response, const char *body, size_t body_length, 
                               const char *content_type, int status_code, const char *status_message) {
    if (!response || !body) return -1;
    
    // Calculate required buffer size
    size_t headers_size = 256; // Approximate headers size
    size_t total_size = headers_size + body_length;
    
    // Allocate response buffer
    response->data = malloc(total_size);
    if (!response->data) return -1;
    
    // Format HTTP response with proper headers
    time_t now;
    time(&now);
    struct tm *tm_info = gmtime(&now);
    char date_buf[128];
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    int written = snprintf(response->data, total_size,
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: AIONIC/1.0\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s", 
        status_code, status_message, date_buf, content_type, body_length, body);
    
    response->length = written;
    response->status_code = status_code;
    response->status_message = (char *)status_message;
    response->is_streaming = 0;
    
    return 0;
}

int route_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    // Initialize response
    memset(response, 0, sizeof(RouteResponse));
    
    // Look for a matching route handler
    for (int i = 0; i < route_count; i++) {
        if (method_matches(routes[i].method, request->method) && 
            strcmp(routes[i].path, request->path) == 0) {
            // Execute the handler
            return routes[i].handler(request, response);
        }
    }
    
    // No matching route found - return a professional 404 page
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
    
    return create_http_response(response, not_found_html, strlen(not_found_html), 
                               "text/html", 404, "Not Found");
}

void free_route_response(RouteResponse *response) {
    if (!response) return;
    
    if (response->data) free(response->data);
    if (response->stream_data) free(response->stream_data);
    
    for (int i = 0; i < response->header_count; i++) {
        if (response->headers[i]) free(response->headers[i]);
    }
    
    memset(response, 0, sizeof(RouteResponse));
}

int register_route(const char *path, HTTPMethod method, int (*handler)(HTTPRequest *, RouteResponse *)) {
    // Expand routes array if needed
    if (route_count >= route_capacity) {
        int new_capacity = route_capacity == 0 ? 8 : route_capacity * 2;
        Route *new_routes = realloc(routes, sizeof(Route) * new_capacity);
        if (!new_routes) return -1;
        
        routes = new_routes;
        route_capacity = new_capacity;
    }
    
    routes[route_count].path = strdup(path);
    routes[route_count].method = method;
    routes[route_count].handler = handler;
    route_count++;
    
    return 0;
}

// Function to handle chat requests using optimized functions
int handle_chat_request_optimized(const HTTPRequest *request, RouteResponse *response) {
    // Extract the prompt from the request
    const char *prompt = NULL;
    if (request->body) {
        // Simple parsing - in a real implementation, you would use proper JSON parsing
        if (strstr(request->body, "\"prompt\"")) {
            prompt = strstr(request->body, "\"prompt\"") + 9;
            if (*prompt == '"') prompt++;
            const char *end = strchr(prompt, '"');
            if (end) {
                size_t prompt_len = end - prompt;
                char *prompt_copy = malloc(prompt_len + 1);
                if (prompt_copy) {
                    // Use optimized memcpy to copy the prompt
                    memcpy_asm(prompt_copy, prompt, prompt_len);
                    prompt_copy[prompt_len] = '\0';
                    
                    // Use optimized CRC32 to compute hash of the prompt
                    uint32_t prompt_hash = crc32_asm(prompt_copy, prompt_len);
                    
                    // Create JSON response with hash information
                    char *json_response = malloc(4096);
                    if (json_response) {
                        snprintf(json_response, 4096, 
                                "{\"response\": \"Processed with optimized functions (hash: %u)\", \"model\": \"aionic-1.0\", \"timestamp\": %ld}", 
                                prompt_hash, time(NULL));
                        
                        // Create HTTP response
                        int result = create_http_response(response, json_response, strlen(json_response), 
                                                         "application/json", 200, "OK");
                        free(json_response);
                        free(prompt_copy);
                        return result;
                    }
                    free(prompt_copy);
                }
            }
        }
    }
    
    // Fallback to error response
    const char *error_response = "{\"error\": \"Invalid request format\"}";
    return create_http_response(response, error_response, strlen(error_response), 
                               "application/json", 400, "Bad Request");
}

int handle_chat_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    // Extract prompt from request body
    char prompt[1024] = {0};
    if (request->body && parse_json(request->body, prompt) == 0) {
        printf("Received prompt: %s\n", prompt);
        
        // In a real implementation, the prompt would be sent to an AI model
        // Here we'll create a sophisticated response
        
        // Create JSON response with AI-generated content
        char *json_response = malloc(4096);
        if (!json_response) {
            return create_http_response(response, "Internal Server Error", 
                                      strlen("Internal Server Error"), 
                                      "text/plain", 500, "Internal Server Error");
        }
        
        snprintf(json_response, 4096, 
                "{\"response\": \"Hello! I've received your message: '%s'. This is a response from the AIONIC AI Web Server.\", \"model\": \"aionic-1.0\", \"timestamp\": %ld}", 
                prompt, time(NULL));
        
        // Create HTTP response
        int result = create_http_response(response, json_response, strlen(json_response), 
                                         "application/json", 200, "OK");
        free(json_response);
        
        return result;
    } else {
        // Error parsing request
        const char *error_html = 
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>400 - Bad Request | AIONIC Server</title>\n"
            "    <style>\n"
            "        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; line-height: 1.6; color: #333; max-width: 800px; margin: 0 auto; padding: 20px; background-color: #f8f9fa; }\n"
            "        .container { background-color: white; border-radius: 8px; box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1); padding: 30px; text-align: center; }\n"
            "        h1 { color: #e67e22; margin-bottom: 20px; }\n"
            "        .home-link { display: inline-block; background-color: #3498db; color: white; padding: 10px 20px; text-decoration: none; border-radius: 4px; transition: background-color 0.3s; }\n"
            "        .home-link:hover { background-color: #2980b9; }\n"
            "        .footer { margin-top: 30px; font-size: 0.9em; color: #7f8c8d; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <h1>400 - Bad Request</h1>\n"
            "        <p>The request could not be understood by the server due to malformed syntax.</p>\n"
            "        <p>Please check your request and try again.</p>\n"
            "        <a href=\"/\" class=\"home-link\">Go to Homepage</a>\n"
            "        <div class=\"footer\">\n"
            "            <p>Powered by AIONIC AI Web Server</p>\n"
            "        </div>\n"
            "    </div>\n"
            "</body>\n"
            "</html>";
        
        return create_http_response(response, error_html, strlen(error_html), 
                                   "text/html", 400, "Bad Request");
    }
}

int handle_stats_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    // In a real implementation, stats would be fetched from the server
    char *stats_json = malloc(1024);
    if (!stats_json) {
        return create_http_response(response, "Internal Server Error", 
                                  strlen("Internal Server Error"), 
                                  "text/plain", 500, "Internal Server Error");
    }
    
    snprintf(stats_json, 1024, 
            "{\"requests\": %lu, \"responses\": %lu, \"uptime\": %ld, \"active_connections\": %d, \"timestamp\": %ld}", 
            (unsigned long)0, (unsigned long)0, (long)0, 0, time(NULL));
    
    int result = create_http_response(response, stats_json, strlen(stats_json), 
                                     "application/json", 200, "OK");
    free(stats_json);
    
    return result;
}

int handle_health_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    const char *health_response = "{\"status\": \"ok\", \"timestamp\": %ld, \"server\": \"AIONIC/1.0\"}";
    char health_json[128];
    snprintf(health_json, sizeof(health_json), health_response, time(NULL));
    
    return create_http_response(response, health_json, strlen(health_json), 
                               "application/json", 200, "OK");
}

// Handle root path request
int handle_root_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
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
    
    return create_http_response(response, root_html, strlen(root_html), 
                               "text/html", 200, "OK");
}

// Function to initialize routes
void init_routes() {
    register_route("/v1/chat", HTTP_POST, handle_chat_request);
    register_route("/stats", HTTP_GET, handle_stats_request);
    register_route("/health", HTTP_GET, handle_health_request);
    
    // Add a default route for the root path
    register_route("/", HTTP_GET, handle_root_request);
}
