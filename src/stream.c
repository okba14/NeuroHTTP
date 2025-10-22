#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

// ===== Project Headers =====
#include "stream.h"
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"

// ===== Constants =====
#define DEFAULT_BUFFER_SIZE 8192
#define DEFAULT_TIMEOUT_MS 5000
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 100

// ===== Static Functions =====
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static StreamResult send_with_timeout(int fd, const void *data, size_t length, uint32_t timeout_ms) {
    if (timeout_ms == 0) {
        // Blocking send
        ssize_t sent = send(fd, data, length, 0);
        return (sent == (ssize_t)length) ? STREAM_SUCCESS : STREAM_ERROR_CLOSED;
    }
    
    // Non-blocking send with timeout
    uint64_t start_time = get_time_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    size_t total_sent = 0;
    
    while (total_sent < length) {
        ssize_t sent = send(fd, (char*)data + total_sent, length - total_sent, MSG_DONTWAIT);
        
        if (sent > 0) {
            total_sent += sent;
        } else if (sent == 0) {
            return STREAM_ERROR_CLOSED;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Check timeout
            uint64_t elapsed = get_time_ns() - start_time;
            if (elapsed >= timeout_ns) {
                return STREAM_ERROR_TIMEOUT;
            }
            
            // Short sleep to avoid busy loop
            struct timespec ts = {0, 10000000}; // 10ms
            nanosleep(&ts, NULL);
        } else {
            return STREAM_ERROR_CLOSED;
        }
    }
    
    return STREAM_SUCCESS;
}

// ===== Original Functions (Maintained for Compatibility) =====

int stream_init(StreamData *stream, int client_fd) {
    StreamConfig default_config = {
        .buffer_size = DEFAULT_BUFFER_SIZE,
        .chunked_encoding = true,
        .timeout_ms = DEFAULT_TIMEOUT_MS,
        .non_blocking = false,
        .priority = 0
    };
    
    return stream_init_ex(stream, client_fd, &default_config);
}

int stream_send_chunk(StreamData *stream, const char *data, size_t length) {
    return stream_send_chunk_ex(stream, data, length, stream->config.timeout_ms);
}

int stream_end(StreamData *stream) {
    if (!stream || !stream->is_active) {
        return -1;
    }
    
    pthread_mutex_lock(&stream->mutex);
    
    // Send stream end
    if (stream->chunked_encoding) {
        const char *final_chunk = "0\r\n\r\n";
        StreamResult result = send_with_timeout(stream->client_fd, final_chunk, 5, stream->config.timeout_ms);
        
        if (result != STREAM_SUCCESS) {
            pthread_mutex_unlock(&stream->mutex);
            return -1;
        }
    }
    
    stream->is_active = false;
    stream->last_activity_ns = get_time_ns();
    
    pthread_mutex_unlock(&stream->mutex);
    return 0;
}

int stream_response(int client_fd, RouteResponse *response) {
    if (!response || client_fd < 0) {
        return -1;
    }
    
    StreamData stream;
    if (stream_init(&stream, client_fd) != 0) {
        return -1;
    }
    
    // Send HTTP header
    char header[1024];
    int header_len = snprintf(header, sizeof(header), 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        response->is_streaming ? "text/plain" : "application/json");
    
    if (send(client_fd, header, header_len, 0) < 0) {
        stream_cleanup(&stream);
        return -1;
    }
    
    // If response is streaming, send data in chunks
    if (response->is_streaming && response->data) {
        // Split data into small chunks
        const char *chunks[] = {
            "Hello",
            " from",
            " AIONIC",
            " AI",
            " Server!",
            "\nThis",
            " is",
            " a",
            " streaming",
            " response."
        };
        
        for (size_t i = 0; i < sizeof(chunks)/sizeof(chunks[0]); i++) {
            if (stream_send_chunk(&stream, chunks[i], strlen(chunks[i])) != 0) {
                stream_cleanup(&stream);
                return -1;
            }
            
            // Wait a bit between chunks
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000; // 100ms
            nanosleep(&ts, NULL);
        }
    } else if (response->data) {
        // Send data as a single chunk
        if (stream_send_chunk(&stream, response->data, response->length) != 0) {
            stream_cleanup(&stream);
            return -1;
        }
    }
    
    // End the stream
    stream_end(&stream);
    stream_cleanup(&stream);
    
    return 0;
}

void stream_cleanup(StreamData *stream) {
    if (!stream) {
        return;
    }
    
    pthread_mutex_lock(&stream->mutex);
    
    if (stream->buffer) {
        free(stream->buffer);
        stream->buffer = NULL;
    }
    
    stream->is_active = false;
    stream->buffer_size = 0;
    stream->buffer_position = 0;
    
    pthread_mutex_unlock(&stream->mutex);
    pthread_mutex_destroy(&stream->mutex);
}

// ===== Enhanced Functions =====

int stream_init_ex(StreamData *stream, int client_fd, const StreamConfig *config) {
    if (!stream || client_fd < 0 || !config) {
        return STREAM_ERROR_NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
        return STREAM_ERROR_MEMORY;
    }
    
    pthread_mutex_lock(&stream->mutex);
    
    // Initialize stream data
    memset(stream, 0, sizeof(StreamData));
    stream->client_fd = client_fd;
    stream->config = *config;
    stream->buffer_size = config->buffer_size > 0 ? config->buffer_size : DEFAULT_BUFFER_SIZE;
    stream->chunked_encoding = config->chunked_encoding;
    stream->is_active = true;
    stream->last_activity_ns = get_time_ns();
    
    // Allocate buffer
    stream->buffer = malloc(stream->buffer_size);
    if (!stream->buffer) {
        pthread_mutex_unlock(&stream->mutex);
        pthread_mutex_destroy(&stream->mutex);
        return STREAM_ERROR_MEMORY;
    }
    
    // Initialize statistics
    memset(&stream->stats, 0, sizeof(StreamStats));
    
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

int stream_send_chunk_ex(StreamData *stream, const char *data, size_t length, uint32_t timeout_ms) {
    if (!stream || !data || length == 0 || !stream->is_active) {
        return STREAM_ERROR_NULL;
    }
    
    pthread_mutex_lock(&stream->mutex);
    
    uint64_t start_time = get_time_ns();
    StreamResult result = STREAM_SUCCESS;
    
    // If chunked encoding, send chunk size first
    if (stream->chunked_encoding) {
        char chunk_header[32];
        int header_len = snprintf(chunk_header, sizeof(chunk_header), "%zx\r\n", length);
        
        result = send_with_timeout(stream->client_fd, chunk_header, header_len, timeout_ms);
        if (result != STREAM_SUCCESS) {
            stream->stats.operations_count++;
            stream->last_activity_ns = get_time_ns();
            pthread_mutex_unlock(&stream->mutex);
            return result;
        }
        
        stream->stats.bytes_sent += header_len;
    }
    
    // Send data
    result = send_with_timeout(stream->client_fd, data, length, timeout_ms);
    if (result != STREAM_SUCCESS) {
        stream->stats.operations_count++;
        stream->last_activity_ns = get_time_ns();
        pthread_mutex_unlock(&stream->mutex);
        return result;
    }
    
    stream->stats.bytes_sent += length;
    
    // Send chunk end if chunked encoding
    if (stream->chunked_encoding) {
        const char *chunk_end = "\r\n";
        result = send_with_timeout(stream->client_fd, chunk_end, 2, timeout_ms);
        if (result != STREAM_SUCCESS) {
            stream->stats.operations_count++;
            stream->last_activity_ns = get_time_ns();
            pthread_mutex_unlock(&stream->mutex);
            return result;
        }
        
        stream->stats.bytes_sent += 2;
    }
    
    stream->stats.chunks_sent++;
    stream->stats.operations_count++;
    stream->last_activity_ns = get_time_ns();
    
    // Update total time
    stream->stats.total_time_ns += get_time_ns() - start_time;
    
    // Call data callback if set
    if (stream->data_callback) {
        stream->data_callback(data, length, stream->user_data);
    }
    
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

StreamResult stream_send_with_callback(StreamData *stream, const char *data, size_t length, 
                                     StreamDataCallback callback, void* user_data) {
    if (!stream || !data || length == 0) {
        return STREAM_ERROR_NULL;
    }
    
    // Set temporary callback
    StreamDataCallback orig_callback = stream->data_callback;
    void* orig_user_data = stream->user_data;
    
    stream->data_callback = callback;
    stream->user_data = user_data;
    
    // Send data
    StreamResult result = (StreamResult)stream_send_chunk_ex(stream, data, length, stream->config.timeout_ms);
    
    // Restore original callback
    stream->data_callback = orig_callback;
    stream->user_data = orig_user_data;
    
    return result;
}

void stream_get_stats(const StreamData *stream, StreamStats *stats) {
    if (!stream || !stats) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&stream->mutex);
    *stats = stream->stats;
    pthread_mutex_unlock((pthread_mutex_t*)&stream->mutex);
}

void stream_set_callbacks(StreamData *stream, StreamDataCallback data_cb, 
                         StreamErrorCallback error_cb, void* user_data) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->mutex);
    stream->data_callback = data_cb;
    stream->error_callback = error_cb;
    stream->user_data = user_data;
    pthread_mutex_unlock(&stream->mutex);
}

// ===== Enhanced Stream Buffer Functions =====

int stream_buffer_init(StreamBuffer *stream, size_t initial_size) {
    return stream_buffer_init_ex(stream, initial_size, true, 1024 * 1024 * 10); // 10MB max
}

int stream_buffer_init_ex(StreamBuffer *stream, size_t initial_size, bool auto_expand, size_t max_capacity) {
    if (!stream) return STREAM_ERROR_NULL;
    
    // Initialize mutex
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
        return STREAM_ERROR_MEMORY;
    }
    
    pthread_mutex_lock(&stream->mutex);
    
    stream->data = malloc(initial_size);
    if (!stream->data) {
        pthread_mutex_unlock(&stream->mutex);
        pthread_mutex_destroy(&stream->mutex);
        return STREAM_ERROR_MEMORY;
    }
    
    stream->size = 0;
    stream->pos = 0;
    stream->capacity = initial_size;
    stream->auto_expand = auto_expand;
    stream->max_capacity = max_capacity;
    
    // Initialize statistics
    memset(&stream->stats, 0, sizeof(StreamStats));
    
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

int stream_buffer_write(StreamBuffer *stream, const void *data, size_t size) {
    return (int)stream_buffer_write_ex(stream, data, size, true);
}

StreamResult stream_buffer_write_ex(StreamBuffer *stream, const void *data, size_t size, bool expand) {
    if (!stream || !data || size == 0) return STREAM_ERROR_NULL;
    
    pthread_mutex_lock(&stream->mutex);
    
    // Check if we need to expand the stream
    if (stream->pos + size > stream->capacity) {
        if (!expand || !stream->auto_expand) {
            pthread_mutex_unlock(&stream->mutex);
            return STREAM_ERROR_OVERFLOW;
        }
        
        size_t new_capacity = stream->capacity * 2;
        while (stream->pos + size > new_capacity) {
            new_capacity *= 2;
        }
        
        // Check max capacity
        if (new_capacity > stream->max_capacity) {
            pthread_mutex_unlock(&stream->mutex);
            return STREAM_ERROR_OVERFLOW;
        }
        
        void *new_data = realloc(stream->data, new_capacity);
        if (!new_data) {
            pthread_mutex_unlock(&stream->mutex);
            return STREAM_ERROR_MEMORY;
        }
        
        stream->data = new_data;
        stream->capacity = new_capacity;
    }
    
    // Use optimized memcpy to copy data
    memcpy_asm(stream->data + stream->pos, data, size);
    stream->pos += size;
    if (stream->pos > stream->size) {
        stream->size = stream->pos;
    }
    
    // Update statistics
    stream->stats.bytes_written += size;
    stream->stats.operations_count++;
    
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

int stream_buffer_read(StreamBuffer *stream, void *data, size_t size) {
    return (int)stream_buffer_read_ex(stream, data, size, true);
}

StreamResult stream_buffer_read_ex(StreamBuffer *stream, void *data, size_t size, bool advance) {
    if (!stream || !data || size == 0) return STREAM_ERROR_NULL;
    
    pthread_mutex_lock(&stream->mutex);
    
    if (stream->pos + size > stream->size) {
        pthread_mutex_unlock(&stream->mutex);
        return STREAM_ERROR_OVERFLOW;
    }
    
    // Use optimized memcpy to copy data
    memcpy_asm(data, stream->data + stream->pos, size);
    
    if (advance) {
        stream->pos += size;
    }
    
    // Update statistics
    stream->stats.bytes_read += size;
    stream->stats.operations_count++;
    
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

void stream_buffer_reset(StreamBuffer *stream) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->mutex);
    stream->pos = 0;
    stream->size = 0;
    pthread_mutex_unlock(&stream->mutex);
}

void stream_buffer_cleanup(StreamBuffer *stream) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->mutex);
    
    if (stream->data) {
        free(stream->data);
        stream->data = NULL;
    }
    
    stream->size = 0;
    stream->pos = 0;
    stream->capacity = 0;
    
    pthread_mutex_unlock(&stream->mutex);
    pthread_mutex_destroy(&stream->mutex);
}

void stream_buffer_get_stats(const StreamBuffer *stream, StreamStats *stats) {
    if (!stream || !stats) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&stream->mutex);
    *stats = stream->stats;
    pthread_mutex_unlock((pthread_mutex_t*)&stream->mutex);
}

// ===== Utility Functions =====

const char* stream_result_to_string(StreamResult result) {
    switch (result) {
        case STREAM_SUCCESS: return "SUCCESS";
        case STREAM_ERROR_NULL: return "NULL_POINTER";
        case STREAM_ERROR_INVALID_FD: return "INVALID_FD";
        case STREAM_ERROR_MEMORY: return "MEMORY_ERROR";
        case STREAM_ERROR_TIMEOUT: return "TIMEOUT";
        case STREAM_ERROR_CLOSED: return "CONNECTION_CLOSED";
        case STREAM_ERROR_OVERFLOW: return "BUFFER_OVERFLOW";
        default: return "UNKNOWN_ERROR";
    }
}

void stream_print_stats(const StreamData *stream) {
    if (!stream) return;
    
    StreamStats stats;
    stream_get_stats(stream, &stats);
    
    printf("Stream Statistics:\n");
    printf("  Bytes Sent: %lu\n", stats.bytes_sent);
    printf("  Bytes Received: %lu\n", stats.bytes_received);
    printf("  Bytes Written: %lu\n", stats.bytes_written);
    printf("  Bytes Read: %lu\n", stats.bytes_read);
    printf("  Chunks Sent: %lu\n", stats.chunks_sent);
    printf("  Operations: %lu\n", stats.operations_count);
    printf("  Total Time: %.3f ms\n", (double)stats.total_time_ns / 1000000.0);
}
