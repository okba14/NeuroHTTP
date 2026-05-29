#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
#include <nghttp2/nghttp2.h>
#include "http2.h"
#include "utils.h"

struct H2Session {
    nghttp2_session *session;
    int fd;
    int is_server;
    H2ConnState state;
    H2Config config;
    pthread_mutex_t mutex;
    char *out_block;
    size_t out_block_len;
    size_t out_block_cap;
    int want_write;
};

#define H2_DEBUG(...) fprintf(stderr, "[H2] " __VA_ARGS__)

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data) {
    (void)session; (void)flags;
    H2Session *s = (H2Session *)user_data;
    size_t new_len = s->out_block_len + length;
    if (new_len > s->out_block_cap) {
        size_t new_cap = s->out_block_cap ? s->out_block_cap * 2 : 65536;
        while (new_cap < new_len) new_cap *= 2;
        char *new_block = realloc(s->out_block, new_cap);
        if (!new_block) return NGHTTP2_ERR_NOMEM;
        s->out_block = new_block;
        s->out_block_cap = new_cap;
    }
    memcpy(s->out_block + s->out_block_len, data, length);
    s->out_block_len += length;
    s->want_write = 1;
    return (ssize_t)length;
}

static int on_begin_headers_callback(nghttp2_session *session,
                                      const nghttp2_frame *frame,
                                      void *user_data) {
    (void)session; (void)frame; (void)user_data;
    return 0;
}

static int on_header_callback(nghttp2_session *session,
                               const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags,
                               void *user_data) {
    (void)session; (void)frame; (void)name; (void)namelen;
    (void)value; (void)valuelen; (void)flags; (void)user_data;
    return 0;
}

static int on_frame_recv_callback(nghttp2_session *session,
                                   const nghttp2_frame *frame,
                                   void *user_data) {
    H2Session *s = (H2Session *)user_data;
    if (frame->hd.type == NGHTTP2_GOAWAY && frame->hd.stream_id == 0) {
        s->state = H2_CONN_STATE_GOAWAY_SENT;
    }
    return 0;
}

static int on_stream_close_callback(nghttp2_session *session,
                                     int32_t stream_id,
                                     uint32_t error_code,
                                     void *user_data) {
    (void)session; (void)stream_id; (void)error_code; (void)user_data;
    return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session,
                                        uint8_t flags,
                                        int32_t stream_id,
                                        const uint8_t *data, size_t len,
                                        void *user_data) {
    (void)session; (void)flags; (void)stream_id;
    H2Session *s = (H2Session *)user_data;
    if (s->config.on_data) {
        s->config.on_data(s->config.user_data, stream_id,
                          (const char *)data, len, 0);
    }
    return 0;
}

static int on_begin_frame_callback(nghttp2_session *session,
                                    const nghttp2_frame_hd *hd,
                                    void *user_data) {
    (void)session; (void)hd; (void)user_data;
    return 0;
}

H2Session *h2_session_new(int fd, const H2Config *cfg, int is_server) {
    H2Session *s = calloc(1, sizeof(H2Session));
    if (!s) return NULL;
    s->fd = fd;
    s->is_server = is_server;
    s->state = H2_CONN_STATE_OPEN;
    if (cfg) memcpy(&s->config, cfg, sizeof(H2Config));
    pthread_mutex_init(&s->mutex, NULL);
    nghttp2_session_callbacks *callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_begin_frame_callback(callbacks, on_begin_frame_callback);
    nghttp2_option *option;
    nghttp2_option_new(&option);
    uint32_t max_streams = cfg->max_concurrent_streams > 0 ?
        cfg->max_concurrent_streams : AIONIC_HTTP2_MAX_CONCURRENT_STREAMS;
    nghttp2_option_set_peer_max_concurrent_streams(option, max_streams);
    nghttp2_option_set_max_reserved_remote_streams(option, 100);
    nghttp2_option_set_no_auto_window_update(option, !cfg->enable_push);
    int rv;
    if (is_server) {
        rv = nghttp2_session_server_new2(&s->session, callbacks, s, option);
    } else {
        rv = nghttp2_session_client_new2(&s->session, callbacks, s, option);
    }
    nghttp2_option_del(option);
    nghttp2_session_callbacks_del(callbacks);
    if (rv != 0) {
        H2_DEBUG("Failed to create nghttp2 session: %s\n", nghttp2_strerror(rv));
        free(s);
        return NULL;
    }
    return s;
}

void h2_session_destroy(H2Session *s) {
    if (!s) return;
    if (s->session) nghttp2_session_del(s->session);
    free(s->out_block);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}

int h2_submit_response(H2Session *s, int32_t stream_id,
                        const char **headers, size_t header_count,
                        const char *body, size_t body_len) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    nghttp2_nv *nv = malloc(sizeof(nghttp2_nv) * header_count);
    if (!nv) return H2_ERR_MEMORY;
    for (size_t i = 0; i < header_count; i++) {
        const char *eq = strchr(headers[i], ':');
        if (!eq) { free(nv); return H2_ERR_PROTOCOL; }
        size_t klen = (size_t)(eq - headers[i]);
        const char *val = eq + 1;
        while (*val == ' ') val++;
        size_t vlen = strlen(val);
        nv[i].name = (uint8_t *)headers[i];
        nv[i].namelen = klen;
        nv[i].value = (uint8_t *)val;
        nv[i].valuelen = vlen;
        nv[i].flags = NGHTTP2_NV_FLAG_NONE;
    }
    int rv;
    if (body && body_len > 0) {
        char *body_copy = malloc(body_len);
        if (!body_copy) { free(nv); return H2_ERR_MEMORY; }
        memcpy(body_copy, body, body_len);
        nghttp2_data_provider2 prd;
        prd.source.ptr = body_copy;
        prd.read_callback = NULL;
        rv = nghttp2_submit_response2(s->session, stream_id, nv, header_count, &prd);
    } else {
        rv = nghttp2_submit_response2(s->session, stream_id, nv, header_count, NULL);
    }
    free(nv);
    if (rv != 0) {
        H2_DEBUG("nghttp2_submit_response2 error: %s\n", nghttp2_strerror(rv));
        return H2_ERR_INTERNAL;
    }
    return h2_session_send(s);
}

int h2_submit_headers(H2Session *s, int32_t stream_id,
                       const char **headers, size_t header_count) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    nghttp2_nv *nv = malloc(sizeof(nghttp2_nv) * header_count);
    if (!nv) return H2_ERR_MEMORY;
    for (size_t i = 0; i < header_count; i++) {
        const char *eq = strchr(headers[i], ':');
        if (!eq) { free(nv); return H2_ERR_PROTOCOL; }
        size_t klen = (size_t)(eq - headers[i]);
        const char *val = eq + 1;
        while (*val == ' ') val++;
        nv[i].name = (uint8_t *)headers[i];
        nv[i].namelen = klen;
        nv[i].value = (uint8_t *)val;
        nv[i].valuelen = strlen(val);
        nv[i].flags = NGHTTP2_NV_FLAG_NONE;
    }
    int rv = nghttp2_submit_headers(s->session, NGHTTP2_FLAG_END_HEADERS,
                                     stream_id, NULL, nv, header_count, NULL);
    free(nv);
    if (rv != 0) return H2_ERR_INTERNAL;
    return h2_session_send(s);
}

int h2_submit_data(H2Session *s, int32_t stream_id,
                    const char *data, size_t len, int complete) {
    (void)data; (void)len; (void)complete;
    if (!s || !s->session) return H2_ERR_INTERNAL;
    uint8_t flags = complete ? NGHTTP2_FLAG_END_STREAM : NGHTTP2_FLAG_NONE;
    nghttp2_data_provider2 prd;
    prd.source.ptr = NULL;
    prd.read_callback = NULL;
    int rv = nghttp2_submit_data2(s->session, flags, stream_id, &prd);
    if (rv != 0) return H2_ERR_INTERNAL;
    return h2_session_send(s);
}

int h2_submit_goaway(H2Session *s, uint32_t last_stream_id, int error_code) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    nghttp2_submit_goaway(s->session, NGHTTP2_FLAG_NONE,
                           last_stream_id, (uint32_t)error_code, NULL, 0);
    s->state = H2_CONN_STATE_GOAWAY_SENT;
    return h2_session_send(s);
}

int h2_submit_rst_stream(H2Session *s, int32_t stream_id, int error_code) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    nghttp2_submit_rst_stream(s->session, NGHTTP2_FLAG_NONE,
                               stream_id, (uint32_t)error_code);
    return h2_session_send(s);
}

int h2_session_upgrade(H2Session *s, const char *settings_payload, size_t settings_len) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    pthread_mutex_lock(&s->mutex);
    int rv = nghttp2_session_upgrade2(s->session, (const uint8_t *)settings_payload,
                                       settings_len, 0, NULL);
    if (rv != 0) {
        H2_DEBUG("nghttp2_session_upgrade2 error: %s\n", nghttp2_strerror(rv));
        pthread_mutex_unlock(&s->mutex);
        return H2_ERR_PROTOCOL;
    }
    rv = h2_session_submit_settings(s);
    if (rv != 0) { pthread_mutex_unlock(&s->mutex); return rv; }
    rv = h2_session_send(s);
    pthread_mutex_unlock(&s->mutex);
    return rv;
}

int h2_session_submit_settings(H2Session *s) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    nghttp2_settings_entry iv[2];
    size_t niv = 0;
    iv[niv].settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
    iv[niv].value = s->config.max_concurrent_streams > 0 ? s->config.max_concurrent_streams : 256;
    niv++;
    iv[niv].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
    iv[niv].value = s->config.initial_window_size > 0 ? s->config.initial_window_size : 65535;
    niv++;
    int rv = nghttp2_submit_settings(s->session, NGHTTP2_FLAG_NONE, iv, niv);
    if (rv != 0) {
        H2_DEBUG("nghttp2_submit_settings error: %s\n", nghttp2_strerror(rv));
        return H2_ERR_INTERNAL;
    }
    return H2_OK;
}

int h2_session_recv(H2Session *s) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    pthread_mutex_lock(&s->mutex);
    if (s->fd < 0) { pthread_mutex_unlock(&s->mutex); return H2_ERR_CLOSED; }
    char buf[65536];
    ssize_t n = recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pthread_mutex_unlock(&s->mutex);
        return H2_OK;
    }
    if (n < 0) { pthread_mutex_unlock(&s->mutex); return H2_ERR_INTERNAL; }
    if (n == 0) { pthread_mutex_unlock(&s->mutex); return H2_ERR_CLOSED; }
    int rv = nghttp2_session_mem_recv(s->session, (const uint8_t *)buf, (size_t)n);
    if (rv < 0) {
        H2_DEBUG("nghttp2_session_mem_recv error: %s\n", nghttp2_strerror(rv));
        pthread_mutex_unlock(&s->mutex);
        return H2_ERR_PROTOCOL;
    }
    if (nghttp2_session_want_write(s->session)) {
        rv = h2_session_send(s);
        pthread_mutex_unlock(&s->mutex);
        return rv;
    }
    pthread_mutex_unlock(&s->mutex);
    return H2_OK;
}

int h2_session_send(H2Session *s) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    s->out_block_len = 0;
    int rv = nghttp2_session_send(s->session);
    if (rv != 0 && rv != NGHTTP2_ERR_EOF) {
        H2_DEBUG("nghttp2_session_send error: %s\n", nghttp2_strerror(rv));
        return H2_ERR_INTERNAL;
    }
    if (s->out_block_len > 0 && s->fd >= 0) {
        size_t off = 0;
        while (off < s->out_block_len) {
            ssize_t n = send(s->fd, s->out_block + off,
                             s->out_block_len - off, MSG_NOSIGNAL);
            if (n <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    struct pollfd pfd = { .fd = s->fd, .events = POLLOUT };
                    poll(&pfd, 1, 10);
                    continue;
                }
                return H2_ERR_INTERNAL;
            }
            off += (size_t)n;
        }
    }
    s->want_write = 0;
    if (nghttp2_session_want_write(s->session)) s->want_write = 1;
    if (rv == NGHTTP2_ERR_EOF) {
        s->state = H2_CONN_STATE_CLOSED;
        return H2_ERR_CLOSED;
    }
    return H2_OK;
}

int h2_session_want_write(H2Session *s) {
    if (!s || !s->session) return 0;
    return s->want_write || nghttp2_session_want_write(s->session);
}

int h2_session_want_read(H2Session *s) {
    if (!s || !s->session) return 0;
    return nghttp2_session_want_read(s->session);
}

H2ConnState h2_session_get_state(const H2Session *s) {
    return s ? s->state : H2_CONN_STATE_CLOSED;
}

void h2_session_set_user_data(H2Session *s, void *user_data) {
    if (!s) return;
    s->config.user_data = user_data;
}

int h2_session_feed_data(H2Session *s, const char *data, size_t len) {
    if (!s || !s->session) return H2_ERR_INTERNAL;
    if (!data || len == 0) return H2_OK;
    pthread_mutex_lock(&s->mutex);
    int rv = nghttp2_session_mem_recv(s->session, (const uint8_t *)data, len);
    if (rv < 0) {
        H2_DEBUG("nghttp2_session_mem_recv error: %s\n", nghttp2_strerror(rv));
        pthread_mutex_unlock(&s->mutex);
        return H2_ERR_PROTOCOL;
    }
    if (nghttp2_session_want_write(s->session)) {
        rv = h2_session_send(s);
        pthread_mutex_unlock(&s->mutex);
        return rv;
    }
    pthread_mutex_unlock(&s->mutex);
    return H2_OK;
}

const char *h2_error_string(H2Error err) {
    switch (err) {
        case H2_OK: return "success";
        case H2_ERR_WANT_READ: return "want read";
        case H2_ERR_WANT_WRITE: return "want write";
        case H2_ERR_CLOSED: return "closed";
        case H2_ERR_MEMORY: return "memory error";
        case H2_ERR_PROTOCOL: return "protocol error";
        case H2_ERR_INTERNAL: return "internal error";
        case H2_ERR_GOAWAY: return "goaway";
        case H2_ERR_REFUSED: return "refused";
        case H2_ERR_FLOW_CONTROL: return "flow control";
        case H2_ERR_STREAM_CLOSED: return "stream closed";
        case H2_ERR_STREAM_ID_REUSE: return "stream id reuse";
        default: return "unknown";
    }
}
