#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>  // ← مهم لـ nanosleep
#include <sys/socket.h>  // ← مهم لـ send
#include "stream.h"
#include "parser.h"
#include "utils.h"

// تهيئة بيانات التدفق
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

// إرسال جزء من البيانات
int stream_send_chunk(StreamData *stream, const char *data, size_t length) {
    if (!stream || !data || length == 0 || !stream->is_active) {
        return -1;
    }
    
    // إذا كان التشفير متقطعاً، أرسل حجم الجزء أولاً
    if (stream->chunked_encoding) {
        char chunk_header[32];
        int header_len = snprintf(chunk_header, sizeof(chunk_header), "%zx\r\n", length);
        
        if (send(stream->client_fd, chunk_header, header_len, 0) < 0) {
            return -1;
        }
    }
    
    // إرسال البيانات
    if (send(stream->client_fd, data, length, 0) < 0) {
        return -1;
    }
    
    // إرسال نهاية الجزء إذا كان التشفير متقطعاً
    if (stream->chunked_encoding) {
        const char *chunk_end = "\r\n";
        if (send(stream->client_fd, chunk_end, 2, 0) < 0) {
            return -1;
        }
    }
    
    return 0;
}

// إنهاء التدفق
int stream_end(StreamData *stream) {
    if (!stream || !stream->is_active) {
        return -1;
    }
    
    // إرسال نهاية التدفق
    if (stream->chunked_encoding) {
        const char *final_chunk = "0\r\n\r\n";
        if (send(stream->client_fd, final_chunk, 5, 0) < 0) {
            return -1;
        }
    }
    
    stream->is_active = 0;
    return 0;
}

// إرسال استجابة متدفقة
int stream_response(int client_fd, RouteResponse *response) {
    if (!response || client_fd < 0) {
        return -1;
    }
    
    StreamData stream;
    if (stream_init(&stream, client_fd) != 0) {
        return -1;
    }
    
    // إرسال رأس HTTP
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
    
    // إذا كانت الاستجابة متدفقة، أرسل البيانات أجزاء
    if (response->is_streaming && response->data) {
        // تقسيم البيانات إلى أجزاء صغيرة
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
            
            // انتظر قليلاً بين الأجزاء
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000000; // 100ms
            nanosleep(&ts, NULL);
        }
    } else if (response->data) {
        // إرسال البيانات كجزء واحد
        if (stream_send_chunk(&stream, response->data, response->length) != 0) {
            stream_cleanup(&stream);
            return -1;
        }
    }
    
    // إنهاء التدفق
    stream_end(&stream);
    stream_cleanup(&stream);
    
    return 0;
}

// تنظيف بيانات التدفق
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
