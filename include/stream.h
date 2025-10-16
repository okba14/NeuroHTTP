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

// Stream buffer structure for in-memory operations
typedef struct {
    char *data;
    size_t size;
    size_t pos;
    size_t capacity;
} StreamBuffer;

// Stream functions
int stream_init(StreamData *stream, int client_fd);
int stream_send_chunk(StreamData *stream, const char *data, size_t length);
int stream_end(StreamData *stream);
int stream_response(int client_fd, RouteResponse *response);
void stream_cleanup(StreamData *stream);

// Stream buffer functions
int stream_buffer_init(StreamBuffer *stream, size_t initial_size);
int stream_buffer_write(StreamBuffer *stream, const void *data, size_t size);
int stream_buffer_read(StreamBuffer *stream, void *data, size_t size);
void stream_buffer_reset(StreamBuffer *stream);
void stream_buffer_cleanup(StreamBuffer *stream);

#endif // AIONIC_STREAM_H
