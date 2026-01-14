#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

// ===== Project Headers =====
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"

// ===== 1. ARENA ALLOCATOR =====
#define ARENA_SIZE 8192

typedef struct {
    char *buf;
    size_t capacity;
    size_t offset;
} Arena;

static void arena_init(Arena *a, char *buffer, size_t size) {
    a->buf = buffer;
    a->capacity = size;
    a->offset = 0;
}

static void* arena_alloc(Arena *a, size_t size) {
    size_t aligned_size = (size + 7) & ~7ULL;
    if (a->offset + aligned_size > a->capacity) return NULL;
    void *ptr = a->buf + a->offset;
    a->offset += aligned_size;
    return ptr;
}

// ===== 2. INLINE CASE-INSENSITIVE COMPARE =====
static inline int fast_casecmp_len(const char *s1, const char *s2, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c1 = (s1[i] >= 'A' && s1[i] <= 'Z') ? s1[i] + 32 : s1[i];
        char c2 = (s2[i] >= 'A' && s2[i] <= 'Z') ? s2[i] + 32 : s2[i];
        if (c1 != c2) return -1;
    }
    return 0;
}

// ===== 3. FAST METHOD PARSING =====
static inline int parse_method_fast(const char *line) {
    uint32_t magic = *(const uint32_t*)line;
    switch (magic) {
        case 0x20544547: return HTTP_GET;
        case 0x504F5354: return HTTP_POST;
        case 0x20555054: return HTTP_PUT;
        case 0x454C4544: return HTTP_DELETE;
        case 0x44414548: return HTTP_HEAD;
        case 0x48435041: return HTTP_PATCH;
        default: return HTTP_UNKNOWN;
    }
}

// ===== 4. PARSE REQUEST LINE =====
static int parse_request_line(const char *line, HTTPRequest *request) {
    char method[16], path[1024], version[16];

    if (sscanf(line, "%15s %1023s %15s", method, path, version) != 3)
        return -1;

    if (strcmp(method, "GET") == 0) request->method = HTTP_GET;
    else if (strcmp(method, "POST") == 0) request->method = HTTP_POST;
    else if (strcmp(method, "PUT") == 0) request->method = HTTP_PUT;
    else if (strcmp(method, "DELETE") == 0) request->method = HTTP_DELETE;
    else if (strcmp(method, "HEAD") == 0) request->method = HTTP_HEAD;
    else if (strcmp(method, "OPTIONS") == 0) request->method = HTTP_OPTIONS;
    else if (strcmp(method, "PATCH") == 0) request->method = HTTP_PATCH;
    else request->method = HTTP_UNKNOWN;

    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';
        request->query_string = strdup(query + 1);
    } else {
        request->query_string = NULL;
    }

    request->path = strdup(path);
    return 0;
}

// ===== 5. HEADER PARSING =====
static int parse_header_line(const char *line, HTTPRequest *request, Arena *arena) {
    const char *colon = strchr(line, ':');
    if (!colon) return -1;

    size_t name_len = colon - line;
    if (name_len >= 64) return 0;

    const char *val_start = colon + 1;
    while (*val_start && isspace((unsigned char)*val_start)) val_start++;
    const char *val_end = val_start;
    while (*val_end && *val_end != '\r' && *val_end != '\n') val_end++;

    size_t val_len = val_end - val_start;

    if (request->header_count < 32) {
        size_t header_size = name_len + 2 + val_len + 1;
        char *header_storage = (char*)arena_alloc(arena, header_size);
        if (header_storage) {
            memcpy(header_storage, line, name_len);
            memcpy(header_storage + name_len, ": ", 2);
            memcpy(header_storage + name_len + 2, val_start, val_len);
            header_storage[header_size - 1] = '\0';
            request->headers[request->header_count++] = header_storage;
        }
    }

    if (name_len == 12 && fast_casecmp_len(line, "Content-Type", 12) == 0) {
        request->content_type = strdup(val_start);
    }

    return 0;
}

// ===== 6. MAIN HTTP PARSER =====
int parse_http_request(const char *raw_request, HTTPRequest *request) {
    if (!raw_request || !request) return -1;
    memset(request, 0, sizeof(HTTPRequest));

    char arena_buffer[ARENA_SIZE];
    Arena arena;
    arena_init(&arena, arena_buffer, sizeof(arena_buffer));

    char *request_copy = strdup(raw_request);
    if (!request_copy) return -1;

    char *line = request_copy;
    char *next_line = strchr(line, '\n');
    if (next_line) *next_line = '\0';

    if (parse_request_line(line, request) != 0) {
        free(request_copy);
        return -1;
    }

    if (next_line) {
        line = next_line + 1;
        while (*line && *line != '\r' && *line != '\n') {
            next_line = strchr(line, '\n');
            if (next_line) *next_line = '\0';
            parse_header_line(line, request, &arena);
            line = next_line ? next_line + 1 : line + strlen(line);
        }
    }

    char *body_start = strstr(raw_request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        if (*body_start) {
            request->body = malloc(strlen(body_start) + 1);
            strcpy(request->body, body_start);
            request->body_length = strlen(body_start);
        }
    }

    if (request->content_type && strstr(request->content_type, "application/json")) {
        if (has_avx2_support()) json_fast_tokenizer_avx2(request->body, request->body_length);
        else json_fast_tokenizer(request->body, request->body_length);
    }

    free(request_copy);
    return 0;
}

// ===== 7. JSON PARSING =====
int parse_json(const char *json_string, void *output, size_t output_size) {
    if (!json_string || !output || output_size == 0) return -1;
    json_fast_tokenizer(json_string, strlen(json_string));

    const char *p = json_string;
    while ((p = strstr(p, "\"prompt\"")) != NULL) {
        p += 8;
        while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

        if (*p == '"') {
            p++;
            char *out = (char*)output;
            size_t i = 0;
            while (*p && *p != '"' && i < output_size - 1) {
                if (*p == '\\') {
                    p++;
                    switch(*p) {
                        case 'n': out[i++] = '\n'; p++; continue;
                        case 't': out[i++] = '\t'; p++; continue;
                        case 'r': out[i++] = '\r'; p++; continue;
                        case '\\': out[i++] = '\\'; p++; continue;
                        case '"': out[i++] = '"'; p++; continue;
                        default: return -1;
                    }
                }
                out[i++] = *p++;
            }
            out[i] = '\0';
            return 0;
        }
    }
    return -1;
}

int json_get_value(const char *json_string, const char *key, char *output, size_t output_size) {
    if (!json_string || !key || !output || output_size == 0) return -1;

    char key_pattern[128];
    int key_len = strlen(key);
    if (key_len > 100) return -1;
    sprintf(key_pattern, "\"%s\"", key);

    const char *key_start = strstr(json_string, key_pattern);
    if (!key_start) return -1;
    key_start += key_len + 2;
    while (*key_start && (*key_start == ' ' || *key_start == ':')) key_start++;

    if (*key_start == '"') {
        key_start++;
        const char *value_end = strchr(key_start, '"');
        if (!value_end) return -1;
        size_t len = value_end - key_start;
        if (len >= output_size) len = output_size - 1;
        memcpy(output, key_start, len);
        output[len] = '\0';
        return 0;
    }
    return -1;
}

// ===== 8. CLEANUP =====
void free_http_request(HTTPRequest *request) {
    if (!request) return;
    if (request->path) free(request->path);
    if (request->query_string) free(request->query_string);
    if (request->body) free(request->body);
    if (request->content_type) free(request->content_type);
    // Headers are in Arena (stack), no manual free needed
    memset(request, 0, sizeof(HTTPRequest));
}

// ===== 9. JSON FAST TOKENIZER STUB =====
int parse_json_with_fast_tokenizer(const char *json_str, size_t length, JSONValue *result) {
    if (has_avx2_support()) json_fast_tokenizer_avx2(json_str, length);
    else json_fast_tokenizer(json_str, length);

    if (result) {
        result->type = JSON_OBJECT;
        result->key = strdup("result");
        result->value.str = strdup("Ultimate Hybrid v5");
        result->value_type = JSON_STRING;
    }
    return 0;
}
