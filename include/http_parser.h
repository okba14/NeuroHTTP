#ifndef AIONIC_HTTP_PARSER_H
#define AIONIC_HTTP_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "parser.h"

typedef struct {
    HTTPMethod method;
    const char *path;
    size_t path_len;
    const char *query;
    size_t query_len;
    const char *headers[64];
    size_t header_lens[64];
    int header_count;
    const char *body;
    size_t body_len;
    const char *content_type;
    size_t content_type_len;
    int http_major;
    int http_minor;
    int keep_alive;
    size_t content_length;
} HTTPRequestParsed;

typedef enum {
    HP_OK = 0,
    HP_ERROR_INCOMPLETE,
    HP_ERROR_INVALID_METHOD,
    HP_ERROR_INVALID_PATH,
    HP_ERROR_INVALID_VERSION,
    HP_ERROR_HEADER_TOO_LARGE,
    HP_ERROR_BODY_TOO_LARGE,
    HP_ERROR_INTERNAL
} HTTPParseError;

typedef enum {
    HP_METHOD_START,
    HP_METHOD,
    HP_SPACE_BEFORE_PATH,
    HP_PATH,
    HP_QUERY,
    HP_SPACE_BEFORE_VERSION,
    HP_VERSION_H,
    HP_VERSION_HT,
    HP_VERSION_HTT,
    HP_VERSION_HTTP,
    HP_VERSION_MAJOR,
    HP_VERSION_MINOR_START,
    HP_VERSION_MINOR,
    HP_CRLF_AFTER_VERSION,
    HP_HEADER_START,
    HP_HEADER_NAME,
    HP_HEADER_SPACE_BEFORE_VALUE,
    HP_HEADER_VALUE,
    HP_HEADER_CRLF,
    HP_HEADERS_COMPLETE_CRLF,
    HP_BODY,
    HP_COMPLETE
} HTTPParserState;

typedef struct {
    HTTPParserState state;
    HTTPMethod method;
    const char *path_start;
    size_t path_len;
    const char *query_start;
    size_t query_len;
    const char *current_header_name_start;
    size_t current_header_name_len;
    const char *current_header_value_start;
    size_t current_header_value_len;
    int header_count;
    size_t content_length;
    int http_major;
    int http_minor;
    int keep_alive;
    const char *body_start;
    size_t body_len;
    const char *content_type_start;
    size_t content_type_len;
    const char *header_ptrs[64];
    size_t header_lens[64];
    int header_finished;
    const char *raw_start;
    size_t raw_len;
} HTTPParser;

void http_parser_init(HTTPParser *p);
HTTPParseError http_parser_execute(HTTPParser *p, const char *data, size_t len);
HTTPParseError http_parser_finish(HTTPParser *p);

#endif
