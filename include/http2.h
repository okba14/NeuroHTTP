#ifndef AIONIC_HTTP2_H
#define AIONIC_HTTP2_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define AIONIC_HTTP2_MAX_CONCURRENT_STREAMS 256
#define AIONIC_HTTP2_MAX_HEADER_LIST_SIZE 65536
#define AIONIC_HTTP2_INITIAL_WINDOW_SIZE 65535
#define AIONIC_HTTP2_DEFAULT_MAX_FRAME_SIZE 16384

typedef enum {
    H2_OK = 0,
    H2_ERR_WANT_READ = -1,
    H2_ERR_WANT_WRITE = -2,
    H2_ERR_CLOSED = -3,
    H2_ERR_MEMORY = -4,
    H2_ERR_PROTOCOL = -5,
    H2_ERR_INTERNAL = -6,
    H2_ERR_GOAWAY = -7,
    H2_ERR_REFUSED = -8,
    H2_ERR_FLOW_CONTROL = -9,
    H2_ERR_STREAM_CLOSED = -10,
    H2_ERR_STREAM_ID_REUSE = -11
} H2Error;

typedef enum {
    H2_CONN_STATE_IDLE,
    H2_CONN_STATE_OPEN,
    H2_CONN_STATE_GOAWAY_SENT,
    H2_CONN_STATE_CLOSING,
    H2_CONN_STATE_CLOSED
} H2ConnState;

typedef struct H2Session H2Session;

typedef void (*H2StreamCallback)(void *user_data, int32_t stream_id,
                                  const char *method, const char *path,
                                  const char **headers, size_t header_count,
                                  const char *body, size_t body_len);

typedef void (*H2StreamDataCB)(void *user_data, int32_t stream_id,
                                const char *data, size_t len, int complete);

typedef struct {
    uint32_t max_concurrent_streams;
    uint32_t max_header_list_size;
    uint32_t initial_window_size;
    uint32_t max_frame_size;
    int enable_push;
    int enable_connect_protocol;
    H2StreamCallback on_request;
    H2StreamDataCB on_data;
    void *user_data;
    int timeout_ms;
} H2Config;

H2Session *h2_session_new(int fd, const H2Config *cfg, int is_server);
void h2_session_destroy(H2Session *session);
int h2_session_upgrade(H2Session *session, const char *settings_payload, size_t settings_len);
int h2_session_submit_settings(H2Session *session);
int h2_session_recv(H2Session *session);
int h2_session_feed_data(H2Session *session, const char *data, size_t len);
int h2_session_send(H2Session *session);
int h2_session_want_write(H2Session *session);
int h2_session_want_read(H2Session *session);
H2ConnState h2_session_get_state(const H2Session *session);

int h2_submit_response(H2Session *session, int32_t stream_id,
                        const char **headers, size_t header_count,
                        const char *body, size_t body_len);

int h2_submit_headers(H2Session *session, int32_t stream_id,
                       const char **headers, size_t header_count);

int h2_submit_data(H2Session *session, int32_t stream_id,
                    const char *data, size_t len, int complete);

int h2_submit_goaway(H2Session *session, uint32_t last_stream_id, int error_code);

int h2_submit_rst_stream(H2Session *session, int32_t stream_id, int error_code);

void h2_session_set_user_data(H2Session *session, void *user_data);

const char *h2_error_string(H2Error err);

#endif
