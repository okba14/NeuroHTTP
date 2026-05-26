#define _POSIX_C_SOURCE 200809L

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
#define ARENA_SIZE 65536

typedef struct {
    char *buf;
    size_t capacity;
    size_t offset;
    int dynamic;  // 1 if buf is heap-allocated
} Arena;

static void arena_init(Arena *a, char *buffer, size_t size) {
    a->buf = buffer;
    a->capacity = size;
    a->offset = 0;
    a->dynamic = 0;
}

static int arena_init_dynamic(Arena *a, size_t size) {
    a->buf = malloc(size);
    if (!a->buf) return -1;
    a->capacity = size;
    a->offset = 0;
    a->dynamic = 1;
    return 0;
}

static void arena_cleanup(Arena *a) {
    if (a->dynamic && a->buf) {
        free(a->buf);
        a->buf = NULL;
    }
}

static void* arena_alloc(Arena *a, size_t size) {
    size_t aligned_size = (size + 7) & ~7ULL;
    if (a->offset + aligned_size > a->capacity) {
        if (!a->dynamic) return NULL;
        size_t new_cap = a->capacity * 2;
        while (a->offset + aligned_size > new_cap) new_cap *= 2;
        char *new_buf = realloc(a->buf, new_cap);
        if (!new_buf) return NULL;
        a->buf = new_buf;
        a->capacity = new_cap;
    }
    void *ptr = a->buf + a->offset;
    a->offset += aligned_size;
    return ptr;
}

// ===== 2. DYNAMIC HTTP PARSER =====
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
    uint32_t magic;
    memcpy_dispatch(&magic, line, sizeof(magic));
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
static int parse_request_line_safe(const char *line, size_t line_len, HTTPRequest *request) {
    (void)line_len;
    char method[16], path[4096], version[16];
    
    int parsed = sscanf(line, "%15s %4095s %15s", method, path, version);
    if (parsed < 2) return -1;
    if (parsed == 2) strcpy(version, "HTTP/1.1");

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
static int parse_header_line(const char *line, size_t line_len, HTTPRequest *request, Arena *arena) {
    (void)line_len;
    const char *colon = (const char *)memchr(line, ':', line_len);
    if (!colon) return -1;

    size_t name_len = colon - line;
    if (name_len == 0 || name_len >= 64) return 0;

    const char *val_start = colon + 1;
    size_t val_remaining = line_len - (val_start - line);
    while (val_remaining > 0 && isspace((unsigned char)*val_start)) {
        val_start++;
        val_remaining--;
    }
    const char *val_end = val_start;
    while (val_remaining > 0 && *val_end != '\r' && *val_end != '\n') {
        val_end++;
        val_remaining--;
    }

    size_t val_len = val_end - val_start;
    if (val_len > 4096) val_len = 4096;

    if (request->header_count < 32) {
        size_t header_size = name_len + 2 + val_len + 1;
        char *header_storage = (char*)arena_alloc(arena, header_size);
        if (header_storage) {
            memcpy_dispatch(header_storage, line, name_len);
            memcpy_dispatch(header_storage + name_len, ": ", 2);
            memcpy_dispatch(header_storage + name_len + 2, val_start, val_len);
            header_storage[header_size - 1] = '\0';
            request->headers[request->header_count++] = header_storage;
        }
    }

    if (name_len == 12 && fast_casecmp_len(line, "Content-Type", 12) == 0) {
        request->content_type = strndup(val_start, val_len);
    }

    return 0;
}

// ===== 6. MAIN HTTP PARSER (Dynamic Safe Version) =====
int parse_http_request_dynamic(const char *raw_request, size_t raw_len, HTTPRequest *request) {
    if (!raw_request || !request || raw_len == 0) return -1;
    memset(request, 0, sizeof(HTTPRequest));

    Arena arena;
    if (arena_init_dynamic(&arena, ARENA_SIZE) != 0) return -1;

    int result = -1;
    char *request_copy = strndup(raw_request, raw_len);
    if (!request_copy) goto cleanup;

    char *line = request_copy;
    size_t remaining = raw_len;

    // Find first line
    char *nl = (char *)memchr(line, '\n', remaining);
    if (nl) {
        size_t line_len = nl - line;
        if (line_len > 0 && line[line_len - 1] == '\r') line_len--;
        *nl = '\0';
        if (parse_request_line_safe(line, line_len, request) != 0) goto cleanup;
        remaining -= (nl - line + 1);
        line = nl + 1;
    } else {
        if (parse_request_line_safe(line, remaining, request) != 0) goto cleanup;
        remaining = 0;
    }

    // Parse headers
    while (remaining > 0 && *line && *line != '\r' && *line != '\n') {
        nl = (char *)memchr(line, '\n', remaining);
        size_t line_len = nl ? (nl - line) : remaining;
        size_t display_len = line_len;
        if (display_len > 0 && line[display_len - 1] == '\r') display_len--;
        
        if (nl) *nl = '\0';
        
        parse_header_line(line, display_len, request, &arena);
        
        if (nl) {
            remaining -= (nl - line + 1);
            line = nl + 1;
        } else {
            break;
        }
    }

    // Skip \r\n\r\n separator
    if (remaining >= 2 && line[0] == '\r' && line[1] == '\n') {
        remaining -= 2;
        line += 2;
    } else if (remaining >= 1 && line[0] == '\n') {
        remaining--;
        line++;
    }

    // Body
    if (remaining > 0) {
        request->body = strndup(line, remaining);
        request->body_length = remaining;
    }

    if (request->content_type && strstr(request->content_type, "application/json")) {
        if (has_avx2_support()) json_fast_tokenizer_avx2(request->body, request->body_length);
        else json_fast_tokenizer(request->body, request->body_length);
    }

    result = 0;

cleanup:
    free(request_copy);
    arena_cleanup(&arena);
    return result;
}

// Legacy wrapper
int parse_http_request(const char *raw_request, HTTPRequest *request) {
    return parse_http_request_dynamic(raw_request, raw_request ? strlen(raw_request) : 0, request);
}

// ===== 7. JSON PARSING (OOB-safe) =====
int parse_json(const char *json_string, void *output, size_t output_size) {
    if (!json_string || !output || output_size == 0) return -1;
    size_t json_len = strlen(json_string);
    json_fast_tokenizer(json_string, json_len);

    if (json_len == 0) return -1;

    const char *end = json_string + json_len;
    const char *p = json_string;
    
    while (p < end) {
        p = (const char *)memchr(p, '"', end - p);
        if (!p) return -1;
        if ((size_t)(end - p) < 8) return -1;
        if (memcmp(p, "\"prompt\"", 8) == 0) {
            p += 8;
            while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) p++;
            if (p < end && *p == '"') {
                p++;
                char *out = (char*)output;
                size_t i = 0;
                while (p < end && *p != '"' && i < output_size - 1) {
                    if (*p == '\\') {
                        p++;
                        if (p >= end) break;
                        switch(*p) {
                            case 'n': out[i++] = '\n'; p++; continue;
                            case 't': out[i++] = '\t'; p++; continue;
                            case 'r': out[i++] = '\r'; p++; continue;
                            case '\\': out[i++] = '\\'; p++; continue;
                            case '"': out[i++] = '"'; p++; continue;
                            default: return -1;
                        }
                    }
                    if (p < end) out[i++] = *p++;
                }
                out[i] = '\0';
                return 0;
            }
        } else {
            p++;
        }
    }
    return -1;
}

int json_get_value(const char *json_string, const char *key, char *output, size_t output_size) {
    if (!json_string || !key || !output || output_size == 0) return -1;
    size_t json_len = strlen(json_string);
    if (json_len == 0) return -1;
    const char *end = json_string + json_len;

    int key_len = strlen(key);
    if (key_len > 100) return -1;

    // Build key pattern dynamically
    char key_pattern[128];
    sprintf(key_pattern, "\"%s\"", key);
    int pattern_len = key_len + 2;

    const char *key_start = json_string;
    while ((key_start = (const char *)memchr(key_start, '"', end - key_start)) != NULL) {
        if ((size_t)(end - key_start) < (size_t)pattern_len) break;
        if (memcmp(key_start, key_pattern, pattern_len) == 0) {
            key_start += pattern_len;
            while (key_start < end && (*key_start == ' ' || *key_start == ':')) key_start++;
            if (key_start < end && *key_start == '"') {
                key_start++;
                const char *value_end = (const char *)memchr(key_start, '"', end - key_start);
                if (!value_end) return -1;
                size_t len = value_end - key_start;
                if (len >= output_size) len = output_size - 1;
                memcpy_dispatch(output, key_start, len);
                output[len] = '\0';
                return 0;
            }
        }
        key_start++;
    }
    return -1;
}

// ===== 8. CLEANUP =====
void free_http_request(HTTPRequest *request) {
    if (!request) return;
    free(request->path);
    free(request->query_string);
    free(request->body);
    free(request->content_type);
    for (int i = 0; i < request->header_count && i < 32; i++) {
        free(request->headers[i]);
        request->headers[i] = NULL;
    }
    request->header_count = 0;
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
