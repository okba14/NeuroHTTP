#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>  // Important for nanosleep
#include <sys/socket.h>  // Important for send
#include "stream.h"
#include "parser.h"
#include "utils.h"
#include "asm_utils.h"

// Initialize stream data
int stream_init(StreamData *stream, int client_fd) {
    if (!stream || client_fd < 0) {
        return -1;
    }
    
    memset(stream, 0, sizeof(StreamData));
    stream->client_fd = client_fd;
    stream->buffer_size = 8192;
    stream->buffer = malloc(stream->buffer_size);
    if (!stream->buffer) {
        return -1;
    }
    
    stream->is_active = 1;
    stream->chunked_encoding = 1;
    
    return 0;
}

// Send a chunk of data
int stream_send_chunk(StreamData *stream, const char *data, size_t length) {
    if (!stream || !data || length == 0 || !stream->is_active) {
        return -1;
    }
    
    // If chunked encoding, send chunk size first
    if (stream->chunked_encoding) {
        char chunk_header[32];
        int header_len = snprintf(chunk_header, sizeof(chunk_header), "%zx\r\n", length);
        
        if (send(stream->client_fd, chunk_header, header_len, 0) < 0) {
            return -1;
        }
    }
    
    // Send data
    if (send(stream->client_fd, data, length, 0) < 0) {
        return -1;
    }
    
    // Send chunk end if chunked encoding
    if (stream->chunked_encoding) {
        const char *chunk_end = "\r\n";
        if (send(stream->client_fd, chunk_end, 2, 0) < 0) {
            return -1;
        }
    }
    
    return 0;
}

// End the stream
int stream_end(StreamData *stream) {
    if (!stream || !stream->is_active) {
        return -1;
    }
    
    // Send stream end
    if (stream->chunked_encoding) {
        const char *final_chunk = "0\r\n\r\n";
        if (send(stream->client_fd, final_chunk, 5, 0) < 0) {
            return -1;
        }
    }
    
    stream->is_active = 0;
    return 0;
}

// Send streaming response
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

// Clean up stream data
void stream_cleanup(StreamData *stream) {
    if (!stream) {
        return;
    }
    
    if (stream->buffer) {
        free(stream->buffer);
        stream->buffer = NULL;
    }
    
    stream->is_active = 0;
}

// Function to initialize a memory buffer stream
int stream_buffer_init(StreamBuffer *stream, size_t initial_size) {
    if (!stream) return -1;
    
    stream->data = malloc(initial_size);
    if (!stream->data) {
        return -1;
    }
    
    stream->size = initial_size;
    stream->pos = 0;
    stream->capacity = initial_size;
    
    return 0;
}

// Function to write data to a stream buffer using optimized memcpy
int stream_buffer_write(StreamBuffer *stream, const void *data, size_t size) {
    if (!stream || !data || size == 0) return -1;
    
    // Check if we need to expand the stream
    if (stream->pos + size > stream->capacity) {
        size_t new_capacity = stream->capacity * 2;
        while (stream->pos + size > new_capacity) {
            new_capacity *= 2;
        }
        
        void *new_data = realloc(stream->data, new_capacity);
        if (!new_data) {
            return -1;
        }
        
        stream->data = new_data;
        stream->capacity = new_capacity;
    }
    
    // Use optimized memcpy to copy data
    memcpy_asm(stream->data + stream->pos, data, size);
    stream->pos += size;
    stream->size = stream->pos;
    
    return 0;
}

// Function to read data from a stream buffer using optimized memcpy
int stream_buffer_read(StreamBuffer *stream, void *data, size_t size) {
    if (!stream || !data || size == 0) return -1;
    
    if (stream->pos + size > stream->size) {
        return -1;  // Not enough data
    }
    
    // Use optimized memcpy to copy data
    memcpy_asm(data, stream->data + stream->pos, size);
    stream->pos += size;
    
    return 0;
}

// Function to reset stream buffer position
void stream_buffer_reset(StreamBuffer *stream) {
    if (!stream) return;
    
    stream->pos = 0;
    stream->size = 0;
}

// Function to clean up stream buffer
void stream_buffer_cleanup(StreamBuffer *stream) {
    if (!stream) return;
    
    if (stream->data) {
        free(stream->data);
        stream->data = NULL;
    }
    
    stream->size = 0;
    stream->pos = 0;
    stream->capacity = 0;
}
