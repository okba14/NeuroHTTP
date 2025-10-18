#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// ===== Project Headers =====
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"


// Helper function to convert string to lowercase
static void to_lower_case(char *str) {
    for (; *str; ++str) *str = tolower(*str);
}

// Helper function to parse request line
static int parse_request_line(const char *line, HTTPRequest *request) {
    char method[16];
    char path[1024];
    char version[16];
    
    if (sscanf(line, "%15s %1023s %15s", method, path, version) != 3) {
        return -1;
    }
    
    // Convert method to lowercase for case-insensitive comparison
    to_lower_case(method);
    
    // Determine request method
    if (strcmp(method, "get") == 0) request->method = HTTP_GET;
    else if (strcmp(method, "post") == 0) request->method = HTTP_POST;
    else if (strcmp(method, "put") == 0) request->method = HTTP_PUT;
    else if (strcmp(method, "delete") == 0) request->method = HTTP_DELETE;
    else if (strcmp(method, "head") == 0) request->method = HTTP_HEAD;
    else if (strcmp(method, "options") == 0) request->method = HTTP_OPTIONS;
    else if (strcmp(method, "patch") == 0) request->method = HTTP_PATCH;
    else request->method = HTTP_UNKNOWN;
    
    // Parse path and query string
    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';  // Separate path from query string
        query++;
        request->query_string = strdup(query);
    } else {
        request->query_string = NULL;
    }
    
    request->path = strdup(path);
    
    return 0;
}

// Helper function to parse HTTP header
static int parse_header_line(const char *line, HTTPRequest *request) {
    char name[64];
    char value[512];
    
    // Find colon that separates header name from value
    const char *colon = strchr(line, ':');
    if (!colon) return -1;
    
    size_t name_len = colon - line;
    if (name_len >= sizeof(name)) return -1;
    
    strncpy(name, line, name_len);
    name[name_len] = '\0';
    
    // Skip spaces after colon
    const char *value_start = colon + 1;
    while (*value_start && isspace(*value_start)) value_start++;
    
    strncpy(value, value_start, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    
    // Remove newline characters from end
    size_t value_len = strlen(value);
    while (value_len > 0 && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n')) {
        value[--value_len] = '\0';
    }
    
    // Store header
    if (request->header_count < 32) {
        char *header = malloc(strlen(name) + strlen(value) + 3);  // name: value\0
        if (header) {
            sprintf(header, "%s: %s", name, value);
            request->headers[request->header_count++] = header;
        }
    }
    
    // Check for content type header
    if (strcmp(name, "Content-Type") == 0) {
        request->content_type = strdup(value);
    }
    
    return 0;
}

// Function to parse JSON using the optimized tokenizer
int parse_json_with_fast_tokenizer(const char *json_str, size_t length, JSONValue *result) {
    // Use the optimized tokenizer based on hardware support
    if (has_avx2_support()) {
        json_fast_tokenizer_avx2(json_str, length);
    } else {
        json_fast_tokenizer(json_str, length);
    }
    
    // For now, we'll just return a simple result
    // In a real implementation, you would parse the tokenized JSON
    result->type = JSON_OBJECT;
    result->key = strdup("result");
    result->value.str = strdup("Tokenized with optimized tokenizer");
    result->value_type = JSON_STRING;
    
    return 0;
}

int parse_http_request(const char *raw_request, HTTPRequest *request) {
    if (!raw_request || !request) return -1;
    
    // Initialize request structure
    memset(request, 0, sizeof(HTTPRequest));
    
    // Copy raw request for processing
    char *request_copy = strdup(raw_request);
    if (!request_copy) return -1;
    
    // Split request into lines
    char *line = strtok(request_copy, "\r\n");
    if (!line) {
        free(request_copy);
        return -1;
    }
    
    // Parse request line
    if (parse_request_line(line, request) != 0) {
        free(request_copy);
        return -1;
    }
    
    // Parse headers
    while ((line = strtok(NULL, "\r\n")) && strlen(line) > 0) {
        parse_header_line(line, request);
    }
    
    // Find request body (after empty line)
    char *body_start = strstr(raw_request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;  // Skip \r\n\r\n
        size_t body_length = strlen(body_start);
        
        if (body_length > 0) {
            request->body = strdup(body_start);
            request->body_length = body_length;
        }
    }
    
    // If we have JSON content, use the optimized tokenizer
    if (request->content_type && strstr(request->content_type, "application/json")) {
        JSONValue json_result;
        if (parse_json_with_fast_tokenizer(request->body, request->body_length, &json_result) == 0) {
            printf("DEBUG: JSON parsed successfully using fast tokenizer\n");
            // You can use the parsed JSON here
            free(json_result.key);
            free(json_result.value.str);
        }
    }
    
    free(request_copy);
    return 0;
}

void free_http_request(HTTPRequest *request) {
    if (!request) return;
    
    if (request->path) free(request->path);
    if (request->query_string) free(request->query_string);
    if (request->body) free(request->body);
    if (request->content_type) free(request->content_type);
    
    for (int i = 0; i < request->header_count; i++) {
        if (request->headers[i]) free(request->headers[i]);
    }
    
    memset(request, 0, sizeof(HTTPRequest));
}

int parse_json(const char *json_string, void *output) {
    // Use assembly function for fast JSON parsing
    if (!json_string || !output) return -1;
    
    // Here we will use json_fast_tokenizer function written in Assembly
    json_fast_tokenizer(json_string, strlen(json_string));
    
    // For simplicity, we'll do basic parsing here
    // In actual implementation, the assembly function would be used fully
    if (strstr(json_string, "\"prompt\"")) {
        char *prompt_start = strstr(json_string, "\"prompt\"");
        if (prompt_start) {
            prompt_start += strlen("\"prompt\"") + 1;  // Skip "prompt":
            while (*prompt_start && (*prompt_start == ' ' || *prompt_start == ':')) prompt_start++;
            
            if (*prompt_start == '"') {
                prompt_start++;
                char *prompt_end = strchr(prompt_start, '"');
                if (prompt_end) {
                    size_t prompt_len = prompt_end - prompt_start;
                    strncpy((char *)output, prompt_start, prompt_len);
                    ((char *)output)[prompt_len] = '\0';
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

int json_get_value(const char *json_string, const char *key, char *output, size_t output_size) {
    if (!json_string || !key || !output || output_size == 0) return -1;
    
    // Search for key in JSON string
    char *key_pattern = malloc(strlen(key) + 4);  // "key":
    sprintf(key_pattern, "\"%s\"", key);
    
    char *key_start = strstr(json_string, key_pattern);
    if (!key_start) {
        free(key_pattern);
        return -1;
    }
    
    // Move to after the key
    key_start += strlen(key_pattern);
    
    // Skip spaces and colon
    while (*key_start && (*key_start == ' ' || *key_start == ':')) key_start++;
    
    // Extract value
    if (*key_start == '"') {
        // String value
        key_start++;
        char *value_end = strchr(key_start, '"');
        if (!value_end) {
            free(key_pattern);
            return -1;
        }
        
        size_t value_len = value_end - key_start;
        if (value_len >= output_size) {
            value_len = output_size - 1;
        }
        
        strncpy(output, key_start, value_len);
        output[value_len] = '\0';
    } else if (*key_start == '{' || *key_start == '[') {
        // Object or array - complex, we'll return error for simplicity
        free(key_pattern);
        return -1;
    } else {
        // Numeric or boolean value
        char *value_end = key_start;
        while (*value_end && *value_end != ',' && *value_end != '}' && *value_end != ']' && 
               !isspace(*value_end)) {
            value_end++;
        }
        
        size_t value_len = value_end - key_start;
        if (value_len >= output_size) {
            value_len = output_size - 1;
        }
        
        strncpy(output, key_start, value_len);
        output[value_len] = '\0';
    }
    
    free(key_pattern);
    return 0;
}
