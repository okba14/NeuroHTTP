#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "http_parser.h"

void http_parser_init(HTTPParser *p) {
    memset(p, 0, sizeof(HTTPParser));
    p->state = HP_METHOD_START;
    p->http_major = 1;
    p->http_minor = 1;
}

static int parse_method(const char *start, size_t len) {
    if (len == 3) {
        if (memcmp(start, "GET", 3) == 0) return HTTP_GET;
        if (memcmp(start, "PUT", 3) == 0) return HTTP_PUT;
    }
    if (len == 4) {
        if (memcmp(start, "POST", 4) == 0) return HTTP_POST;
        if (memcmp(start, "HEAD", 4) == 0) return HTTP_HEAD;
    }
    if (len == 5) {
        if (memcmp(start, "PATCH", 5) == 0) return HTTP_PATCH;
    }
    if (len == 6) {
        if (memcmp(start, "DELETE", 6) == 0) return HTTP_DELETE;
        if (memcmp(start, "OPTIONS", 7) == 0) return HTTP_OPTIONS;
    }
    return HTTP_UNKNOWN;
}

static int is_header_char(char c) {
    return (unsigned char)c > 32 && c != 127;
}

HTTPParseError http_parser_execute(HTTPParser *p, const char *data, size_t len) {
    if (!p || !data) return HP_ERROR_INTERNAL;
    if (p->state == HP_COMPLETE) return HP_OK;

    if (!p->raw_start) {
        p->raw_start = data;
        p->raw_len = 0;
    }

    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        p->raw_len++;

        switch (p->state) {
            case HP_METHOD_START:
                if (c == ' ' || c == '\r' || c == '\n') return HP_ERROR_INVALID_METHOD;
                p->path_start = data + i;
                p->state = HP_METHOD;
                break;

            case HP_SPACE_BEFORE_PATH:
                if (c == '/') { p->path_start = data + i; p->path_len = 0; p->state = HP_PATH; }
                break;

            case HP_METHOD:
                if (c == ' ') {
                    size_t method_len = (data + i) - p->path_start;
                    p->method = parse_method(p->path_start, method_len);
                    if (p->method == HTTP_UNKNOWN) return HP_ERROR_INVALID_METHOD;
                    p->path_start = data + i + 1;
                    p->state = HP_PATH;
                }
                break;

            case HP_PATH:
                if (c == '?') {
                    p->path_len = (data + i) - p->path_start;
                    p->query_start = data + i + 1;
                    p->state = HP_QUERY;
                } else if (c == ' ') {
                    p->path_len = (data + i) - p->path_start;
                    p->state = HP_SPACE_BEFORE_VERSION;
                }
                break;

            case HP_QUERY:
                if (c == ' ') {
                    p->query_len = (data + i) - p->query_start;
                    p->state = HP_SPACE_BEFORE_VERSION;
                }
                break;

            case HP_SPACE_BEFORE_VERSION:
                if (c == 'H') p->state = HP_VERSION_H;
                break;

            case HP_VERSION_H:
                p->state = (c == 'T') ? HP_VERSION_HT : HP_SPACE_BEFORE_VERSION;
                break;
            case HP_VERSION_HT:
                p->state = (c == 'T') ? HP_VERSION_HTT : HP_SPACE_BEFORE_VERSION;
                break;
            case HP_VERSION_HTT:
                p->state = (c == 'P') ? HP_VERSION_HTTP : HP_SPACE_BEFORE_VERSION;
                break;
            case HP_VERSION_HTTP:
                p->state = (c == '/') ? HP_VERSION_MAJOR : HP_SPACE_BEFORE_VERSION;
                break;

            case HP_VERSION_MAJOR:
                if (c >= '0' && c <= '9') {
                    p->http_major = c - '0';
                    p->state = HP_VERSION_MINOR_START;
                }
                break;

            case HP_VERSION_MINOR_START:
                if (c == '.') p->state = HP_VERSION_MINOR;
                break;

            case HP_VERSION_MINOR:
                if (c >= '0' && c <= '9') {
                    p->http_minor = c - '0';
                } else if (c == '\r') {
                    p->state = HP_CRLF_AFTER_VERSION;
                }
                break;

            case HP_CRLF_AFTER_VERSION:
                p->state = (c == '\n') ? HP_HEADER_START : HP_CRLF_AFTER_VERSION;
                break;

            case HP_HEADER_START:
                if (c == '\r') {
                    p->state = HP_HEADERS_COMPLETE_CRLF;
                } else if (c == '\n') {
                    p->state = HP_BODY;
                } else if (is_header_char(c)) {
                    p->current_header_name_start = data + i;
                    p->current_header_name_len = 1;
                    p->state = HP_HEADER_NAME;
                }
                break;

            case HP_HEADER_NAME:
                if (c == ':') {
                    p->current_header_value_start = data + i + 1;
                    p->current_header_value_len = 0;
                    p->state = HP_HEADER_SPACE_BEFORE_VALUE;
                } else if (is_header_char(c)) {
                    p->current_header_name_len++;
                } else {
                    return HP_ERROR_INCOMPLETE;
                }
                break;

            case HP_HEADER_SPACE_BEFORE_VALUE:
                if (c == ' ' || c == '\t') {
                    break;
                }
                p->current_header_value_start = data + i;
                p->current_header_value_len = 1;
                p->state = HP_HEADER_VALUE;
                break;

            case HP_HEADER_VALUE: {
                size_t name_len = (data + i) - p->current_header_name_start;
                if (c == '\r') {
                    size_t val_len = (data + i) - p->current_header_value_start;
                    p->current_header_value_len = val_len;
                    if (p->header_count < 64) {
                        int idx = p->header_count++;
                        p->header_ptrs[idx] = p->current_header_name_start;
                        p->header_lens[idx] = name_len;
                    }
                    if (name_len == 14) {
                        char lower[15];
                        for (size_t j = 0; j < 14 && j < name_len; j++)
                            lower[j] = tolower((unsigned char)p->current_header_name_start[j]);
                        lower[14] = 0;
                        if (memcmp(lower, "content-length", 14) == 0) {
                            char num_buf[32];
                            size_t vl = val_len > 31 ? 31 : val_len;
                            memcpy(num_buf, p->current_header_value_start, vl);
                            num_buf[vl] = 0;
                            p->content_length = strtoull(num_buf, NULL, 10);
                        }
                        if (memcmp(lower, "content-type", 12) == 0) {
                            p->content_type_start = p->current_header_value_start;
                            p->content_type_len = val_len;
                        }
                    }
                    if (name_len == 10) {
                        char lower[11];
                        for (size_t j = 0; j < 10 && j < name_len; j++)
                            lower[j] = tolower((unsigned char)p->current_header_name_start[j]);
                        lower[10] = 0;
                        if (memcmp(lower, "connection", 10) == 0) {
                            char val_lower[32];
                            size_t vl = val_len > 31 ? 31 : val_len;
                            for (size_t j = 0; j < vl; j++)
                                val_lower[j] = tolower((unsigned char)p->current_header_value_start[j]);
                            val_lower[vl] = 0;
                            p->keep_alive = (strstr(val_lower, "keep-alive") != NULL);
                        }
                    }
                    p->state = HP_HEADER_CRLF;
                }
                break;
            }

            case HP_HEADER_CRLF:
                if (c == '\n') {
                    p->state = HP_HEADER_START;
                }
                break;

            case HP_HEADERS_COMPLETE_CRLF:
                if (c == '\n') {
                    p->body_start = data + i + 1;
                    p->body_len = len - (i + 1);
                    p->state = HP_BODY;
                }
                break;

            case HP_BODY:
                p->body_len = len - (p->body_start - data);
                break;

            case HP_COMPLETE:
                break;
        }
    }

    if (p->state == HP_BODY) {
        if (p->content_length > 0 && p->body_len >= p->content_length) {
            p->body_len = p->content_length;
            p->state = HP_COMPLETE;
        } else if (p->content_length == 0) {
            p->state = HP_COMPLETE;
        }
    }

    return HP_OK;
}

HTTPParseError http_parser_finish(HTTPParser *p) {
    if (p->state == HP_BODY || p->state == HP_HEADER_START || p->state == HP_HEADERS_COMPLETE_CRLF) {
        p->state = HP_COMPLETE;
        return HP_OK;
    }
    return p->state == HP_COMPLETE ? HP_OK : HP_ERROR_INCOMPLETE;
}
