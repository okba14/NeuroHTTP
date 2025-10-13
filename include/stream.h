#ifndef AIONIC_STREAM_H
#define AIONIC_STREAM_H

#include "parser.h"


typedef struct {
    int client_fd;
    char *buffer;
    size_t buffer_size;
    size_t buffer_position;
    int is_active;
    int chunked_encoding;
} StreamData;


int stream_init(StreamData *stream, int client_fd);
int stream_send_chunk(StreamData *stream, const char *data, size_t length);
int stream_end(StreamData *stream);
int stream_response(int client_fd, RouteResponse *response);
void stream_cleanup(StreamData *stream);

#endif // AIONIC_STREAM_H
