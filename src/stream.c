#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include "stream.h"
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"

#define DEFAULT_BUFFER_SIZE 8192
#define DEFAULT_TIMEOUT_MS 5000
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 100

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static StreamResult send_with_timeout(int fd, const void *data, size_t length, uint32_t timeout_ms) {
    if (timeout_ms == 0) {
        ssize_t sent = send(fd, data, length, 0);
        return (sent == (ssize_t)length) ? STREAM_SUCCESS : STREAM_ERROR_CLOSED;
    }
    uint64_t start_time = get_time_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(fd, (char*)data + total_sent, length - total_sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) total_sent += sent;
        else if (sent == 0) return STREAM_ERROR_CLOSED;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (get_time_ns() - start_time >= timeout_ns) return STREAM_ERROR_TIMEOUT;
            struct timespec ts = {0, 10000000};
            nanosleep(&ts, NULL);
        } else return STREAM_ERROR_CLOSED;
    }
    return STREAM_SUCCESS;
}

int stream_init(StreamData *stream, int client_fd) {
    StreamConfig default_config = { .buffer_size = DEFAULT_BUFFER_SIZE, .chunked_encoding = true, .timeout_ms = DEFAULT_TIMEOUT_MS, .non_blocking = false, .priority = 0 };
    return stream_init_ex(stream, client_fd, &default_config);
}

int stream_send_chunk(StreamData *stream, const char *data, size_t length) {
    return stream_send_chunk_ex(stream, data, length, stream->config.timeout_ms);
}

int stream_end(StreamData *stream) {
    if (!stream || !stream->is_active) return -1;
    pthread_mutex_lock(&stream->mutex);
    if (stream->chunked_encoding) {
        const char *final_chunk = "0\r\n\r\n";
        StreamResult result = send_with_timeout(stream->client_fd, final_chunk, 5, stream->config.timeout_ms);
        if (result != STREAM_SUCCESS) { pthread_mutex_unlock(&stream->mutex); return -1; }
    }
    stream->is_active = false;
    stream->last_activity_ns = get_time_ns();
    pthread_mutex_unlock(&stream->mutex);
    return 0;
}

int stream_response(int client_fd, RouteResponse *response) {
    if (!response || client_fd < 0) return -1;
    StreamData stream;
    if (stream_init(&stream, client_fd) != 0) return -1;
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        response->is_streaming ? "text/plain" : "application/json");
    if (send(client_fd, header, header_len, 0) < 0) { stream_cleanup(&stream); return -1; }
    if (response->is_streaming && response->data) {
        size_t chunk_size = 32;
        char *data_ptr = response->data;
        size_t remaining = strlen(data_ptr);
        while (remaining > 0) {
            size_t to_send = (remaining < chunk_size) ? remaining : chunk_size;
            if (stream_send_chunk(&stream, data_ptr, to_send) != 0) { stream_cleanup(&stream); return -1; }
            data_ptr += to_send;
            remaining -= to_send;
            struct timespec ts = {0, 50000000};
            nanosleep(&ts, NULL);
        }
    } else if (response->data) {
        if (stream_send_chunk(&stream, response->data, response->length) != 0) { stream_cleanup(&stream); return -1; }
    }
    stream_end(&stream);
    stream_cleanup(&stream);
    return 0;
}

void stream_cleanup(StreamData *stream) {
    if (!stream) return;
    pthread_mutex_lock(&stream->mutex);
    free(stream->buffer);
    stream->buffer = NULL;
    stream->is_active = false;
    stream->buffer_size = 0;
    stream->buffer_position = 0;
    pthread_mutex_unlock(&stream->mutex);
    pthread_mutex_destroy(&stream->mutex);
}

int stream_init_ex(StreamData *stream, int client_fd, const StreamConfig *config) {
    if (!stream || client_fd < 0 || !config) return STREAM_ERROR_NULL;
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) return STREAM_ERROR_MEMORY;
    pthread_mutex_lock(&stream->mutex);
    memset(stream, 0, sizeof(StreamData));
    stream->client_fd = client_fd;
    stream->config = *config;
    stream->buffer_size = config->buffer_size > 0 ? config->buffer_size : DEFAULT_BUFFER_SIZE;
    stream->chunked_encoding = config->chunked_encoding;
    stream->is_active = true;
    stream->last_activity_ns = get_time_ns();
    stream->buffer = malloc(stream->buffer_size);
    if (!stream->buffer) { pthread_mutex_unlock(&stream->mutex); pthread_mutex_destroy(&stream->mutex); return STREAM_ERROR_MEMORY; }
    memset(&stream->stats, 0, sizeof(StreamStats));
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

int stream_send_chunk_ex(StreamData *stream, const char *data, size_t length, uint32_t timeout_ms) {
    if (!stream || !data || length == 0 || !stream->is_active) return STREAM_ERROR_NULL;
    pthread_mutex_lock(&stream->mutex);
    uint64_t start_time = get_time_ns();
    StreamResult result = STREAM_SUCCESS;
    if (stream->chunked_encoding) {
        char chunk_header[32];
        int header_len = snprintf(chunk_header, sizeof(chunk_header), "%zx\r\n", length);
        result = send_with_timeout(stream->client_fd, chunk_header, header_len, timeout_ms);
        if (result != STREAM_SUCCESS) { pthread_mutex_unlock(&stream->mutex); return result; }
        stream->stats.bytes_sent += header_len;
    }
    result = send_with_timeout(stream->client_fd, data, length, timeout_ms);
    if (result != STREAM_SUCCESS) { pthread_mutex_unlock(&stream->mutex); return result; }
    stream->stats.bytes_sent += length;
    if (stream->chunked_encoding) {
        const char *chunk_end = "\r\n";
        result = send_with_timeout(stream->client_fd, chunk_end, 2, timeout_ms);
        if (result != STREAM_SUCCESS) { pthread_mutex_unlock(&stream->mutex); return result; }
        stream->stats.bytes_sent += 2;
    }
    stream->stats.chunks_sent++;
    stream->stats.operations_count++;
    stream->last_activity_ns = get_time_ns();
    stream->stats.total_time_ns += get_time_ns() - start_time;
    if (stream->data_callback) stream->data_callback(data, length, stream->user_data);
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

int stream_send_sse(StreamData *stream, const char *event, const char *data, size_t data_len) {
    if (!stream || !data) return -1;
    char header[256];
    int hlen = 0;
    if (event) hlen = snprintf(header, sizeof(header), "event: %s\r\n", event);
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "id: %lu\r\n", (unsigned long)(get_time_ns() % 1000000));
    stream_send_chunk_ex(stream, id_buf, strlen(id_buf), stream->config.timeout_ms);
    if (hlen > 0) stream_send_chunk_ex(stream, header, hlen, stream->config.timeout_ms);
    char data_header[32];
    int dhlen = snprintf(data_header, sizeof(data_header), "data: ");
    stream_send_chunk_ex(stream, data_header, dhlen, stream->config.timeout_ms);
    stream_send_chunk_ex(stream, data, data_len, stream->config.timeout_ms);
    stream_send_chunk_ex(stream, "\r\n\r\n", 4, stream->config.timeout_ms);
    return 0;
}

StreamResult stream_send_with_callback(StreamData *stream, const char *data, size_t length, StreamDataCallback callback, void* user_data) {
    if (!stream || !data || length == 0) return STREAM_ERROR_NULL;
    StreamDataCallback orig_callback = stream->data_callback;
    void* orig_user_data = stream->user_data;
    stream->data_callback = callback;
    stream->user_data = user_data;
    StreamResult result = (StreamResult)stream_send_chunk_ex(stream, data, length, stream->config.timeout_ms);
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

void stream_set_callbacks(StreamData *stream, StreamDataCallback data_cb, StreamErrorCallback error_cb, void* user_data) {
    if (!stream) return;
    pthread_mutex_lock(&stream->mutex);
    stream->data_callback = data_cb;
    stream->error_callback = error_cb;
    stream->user_data = user_data;
    pthread_mutex_unlock(&stream->mutex);
}

int stream_buffer_init(StreamBuffer *stream, size_t initial_size) {
    return stream_buffer_init_ex(stream, initial_size, true, 1024 * 1024 * 10);
}

int stream_buffer_init_ex(StreamBuffer *stream, size_t initial_size, bool auto_expand, size_t max_capacity) {
    if (!stream) return STREAM_ERROR_NULL;
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) return STREAM_ERROR_MEMORY;
    pthread_mutex_lock(&stream->mutex);
    stream->data = malloc(initial_size);
    if (!stream->data) { pthread_mutex_unlock(&stream->mutex); pthread_mutex_destroy(&stream->mutex); return STREAM_ERROR_MEMORY; }
    stream->size = 0; stream->pos = 0; stream->capacity = initial_size;
    stream->auto_expand = auto_expand; stream->max_capacity = max_capacity;
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
    if (stream->pos + size > stream->capacity) {
        if (!expand || !stream->auto_expand) { pthread_mutex_unlock(&stream->mutex); return STREAM_ERROR_OVERFLOW; }
        size_t new_capacity = stream->capacity * 2;
        while (stream->pos + size > new_capacity) new_capacity *= 2;
        if (new_capacity > stream->max_capacity) { pthread_mutex_unlock(&stream->mutex); return STREAM_ERROR_OVERFLOW; }
        void *new_data = realloc(stream->data, new_capacity);
        if (!new_data) { pthread_mutex_unlock(&stream->mutex); return STREAM_ERROR_MEMORY; }
        stream->data = new_data; stream->capacity = new_capacity;
    }
    memcpy_dispatch(stream->data + stream->pos, data, size);
    stream->pos += size;
    if (stream->pos > stream->size) stream->size = stream->pos;
    stream->stats.bytes_written += size; stream->stats.operations_count++;
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

int stream_buffer_read(StreamBuffer *stream, void *data, size_t size) {
    return (int)stream_buffer_read_ex(stream, data, size, true);
}

StreamResult stream_buffer_read_ex(StreamBuffer *stream, void *data, size_t size, bool advance) {
    if (!stream || !data || size == 0) return STREAM_ERROR_NULL;
    pthread_mutex_lock(&stream->mutex);
    if (stream->pos + size > stream->size) { pthread_mutex_unlock(&stream->mutex); return STREAM_ERROR_OVERFLOW; }
    memcpy_dispatch(data, stream->data + stream->pos, size);
    if (advance) stream->pos += size;
    stream->stats.bytes_read += size; stream->stats.operations_count++;
    pthread_mutex_unlock(&stream->mutex);
    return STREAM_SUCCESS;
}

void stream_buffer_reset(StreamBuffer *stream) {
    if (!stream) return;
    pthread_mutex_lock(&stream->mutex);
    stream->pos = 0; stream->size = 0;
    pthread_mutex_unlock(&stream->mutex);
}

void stream_buffer_cleanup(StreamBuffer *stream) {
    if (!stream) return;
    pthread_mutex_lock(&stream->mutex);
    free(stream->data); stream->data = NULL;
    stream->size = 0; stream->pos = 0; stream->capacity = 0;
    pthread_mutex_unlock(&stream->mutex);
    pthread_mutex_destroy(&stream->mutex);
}

void stream_buffer_get_stats(const StreamBuffer *stream, StreamStats *stats) {
    if (!stream || !stats) return;
    pthread_mutex_lock((pthread_mutex_t*)&stream->mutex);
    *stats = stream->stats;
    pthread_mutex_unlock((pthread_mutex_t*)&stream->mutex);
}

const char* stream_result_to_string(StreamResult result) {
    switch (result) {
        case STREAM_SUCCESS: return "SUCCESS"; case STREAM_ERROR_NULL: return "NULL_POINTER";
        case STREAM_ERROR_INVALID_FD: return "INVALID_FD"; case STREAM_ERROR_MEMORY: return "MEMORY_ERROR";
        case STREAM_ERROR_TIMEOUT: return "TIMEOUT"; case STREAM_ERROR_CLOSED: return "CONNECTION_CLOSED";
        case STREAM_ERROR_OVERFLOW: return "BUFFER_OVERFLOW"; default: return "UNKNOWN_ERROR";
    }
}

void stream_print_stats(const StreamData *stream) {
    if (!stream) return;
    StreamStats stats; stream_get_stats(stream, &stats);
    printf("Stream Stats: Sent=%lu Chunks=%lu Time=%.3fms\n", (unsigned long)stats.bytes_sent,
           (unsigned long)stats.chunks_sent, (double)stats.total_time_ns / 1000000.0);
}
