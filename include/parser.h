#ifndef AIONIC_PARSER_H
#define AIONIC_PARSER_H

#include <stdint.h>
#include <stddef.h>


typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_PATCH,
    HTTP_UNKNOWN
} HTTPMethod;


typedef struct {
    HTTPMethod method;
    char *path;
    char *query_string;
    char *headers[32];  
    int header_count;
    char *body;
    size_t body_length;
} HTTPRequest;


typedef struct {
    int status_code;
    char *status_message;
    char *headers[32];
    int header_count;
    char *data;
    size_t length;
    int is_streaming;
    void *stream_data;
} RouteResponse;


int parse_http_request(const char *raw_request, HTTPRequest *request);
void free_http_request(HTTPRequest *request);
int parse_json(const char *json_string, void *output);
int json_get_value(const char *json_string, const char *key, char *output, size_t output_size);


extern void json_fast_tokenizer(const char *json_string, size_t length);
extern void *memcpy_asm(void *dest, const void *src, size_t n);
extern uint32_t crc32_asm(const unsigned char *data, size_t length);

#endif // AIONIC_PARSER_H
