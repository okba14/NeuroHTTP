#ifndef AIONIC_STREAM_H
#define AIONIC_STREAM_H

#include "parser.h"
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

// Stream operation result codes
typedef enum {
    STREAM_SUCCESS = 0,
    STREAM_ERROR_NULL,
    STREAM_ERROR_INVALID_FD,
    STREAM_ERROR_MEMORY,
    STREAM_ERROR_TIMEOUT,
    STREAM_ERROR_CLOSED,
    STREAM_ERROR_OVERFLOW
} StreamResult;

// Stream statistics structure
typedef struct {
    uint64_t bytes_sent;        // For network streams: bytes sent
    uint64_t bytes_received;    // For network streams: bytes received
    uint64_t chunks_sent;       // For network streams: number of chunks sent
    uint64_t operations_count;   // For both: number of operations
    uint64_t total_time_ns;     // For both: total time in nanoseconds
    uint64_t bytes_written;     // For buffer streams: bytes written
    uint64_t bytes_read;        // For buffer streams: bytes read
} StreamStats;

// Stream configuration options
typedef struct {
    size_t buffer_size;
    bool chunked_encoding;
    uint32_t timeout_ms;
    bool non_blocking;
    uint8_t priority;
} StreamConfig;

// Stream callback function types
typedef void (*StreamDataCallback)(const char* data, size_t length, void* user_data);
typedef void (*StreamErrorCallback)(StreamResult error, void* user_data);

// Original StreamData structure with enhancements
typedef struct {
    int client_fd;
    char *buffer;
    size_t buffer_size;
    size_t buffer_position;
    bool is_active;
    bool chunked_encoding;
    
    // Enhanced features
    pthread_mutex_t mutex;
    StreamConfig config;
    StreamStats stats;
    StreamDataCallback data_callback;
    StreamErrorCallback error_callback;
    void* user_data;
    uint64_t last_activity_ns;
} StreamData;

// Enhanced StreamBuffer structure
typedef struct {
    char *data;
    size_t size;
    size_t pos;
    size_t capacity;
    
    // Enhanced features
    pthread_mutex_t mutex;
    StreamStats stats;
    bool auto_expand;
    size_t max_capacity;
} StreamBuffer;

// Original stream functions (maintained for compatibility)
int stream_init(StreamData *stream, int client_fd);
int stream_send_chunk(StreamData *stream, const char *data, size_t length);
int stream_end(StreamData *stream);
int stream_response(int client_fd, RouteResponse *response);
void stream_cleanup(StreamData *stream);

// Enhanced stream functions
int stream_init_ex(StreamData *stream, int client_fd, const StreamConfig *config);
int stream_send_chunk_ex(StreamData *stream, const char *data, size_t length, uint32_t timeout_ms);
StreamResult stream_send_with_callback(StreamData *stream, const char *data, size_t length, 
                                     StreamDataCallback callback, void* user_data);
void stream_get_stats(const StreamData *stream, StreamStats *stats);
void stream_set_callbacks(StreamData *stream, StreamDataCallback data_cb, 
                         StreamErrorCallback error_cb, void* user_data);

// Original stream buffer functions (maintained for compatibility)
int stream_buffer_init(StreamBuffer *stream, size_t initial_size);
int stream_buffer_write(StreamBuffer *stream, const void *data, size_t size);
int stream_buffer_read(StreamBuffer *stream, void *data, size_t size);
void stream_buffer_reset(StreamBuffer *stream);
void stream_buffer_cleanup(StreamBuffer *stream);

// Enhanced stream buffer functions
int stream_buffer_init_ex(StreamBuffer *stream, size_t initial_size, bool auto_expand, size_t max_capacity);
StreamResult stream_buffer_write_ex(StreamBuffer *stream, const void *data, size_t size, bool expand);
StreamResult stream_buffer_read_ex(StreamBuffer *stream, void *data, size_t size, bool advance);
void stream_buffer_get_stats(const StreamBuffer *stream, StreamStats *stats);

// Utility functions
const char* stream_result_to_string(StreamResult result);
void stream_print_stats(const StreamData *stream);

#endif // AIONIC_STREAM_H
