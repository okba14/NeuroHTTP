#ifndef AIONIC_TLS_H
#define AIONIC_TLS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define AIONIC_TLS_MAX_CERT_CHAIN 8
#define AIONIC_TLS_OCSP_MAX_AGE 86400
#define AIONIC_TLS_SESSION_CACHE_SIZE 2048
#define AIONIC_TLS_ALPN_STRING_LEN 128

typedef enum {
    TLS_OK = 0,
    TLS_ERR_WANT_READ = -1,
    TLS_ERR_WANT_WRITE = -2,
    TLS_ERR_WANT_CONNECT = -3,
    TLS_ERR_WANT_ACCEPT = -4,
    TLS_ERR_CLOSED = -5,
    TLS_ERR_SYSTEM = -6,
    TLS_ERR_MEMORY = -7,
    TLS_ERR_CERT = -8,
    TLS_ERR_VERIFY = -9,
    TLS_ERR_OCSP = -10
} TLSError;

typedef enum {
    TLS_METHOD_AUTO = 0,
    TLS_METHOD_TLSv1_2,
    TLS_METHOD_TLSv1_3
} TLSMethod;

typedef struct {
    char cert_file[4096];
    char key_file[4096];
    char ca_file[4096];
    char ca_path[4096];
    char cipher_list[256];
    char cipher_suites_tls13[256];
    char session_ticket_key_file[4096];
    TLSMethod min_method;
    int enable_ocsp_stapling;
    int enable_session_tickets;
    int prefer_server_ciphers;
    int verify_client;
    int verify_depth;
    int session_cache_size;
    int ocsp_refresh_interval;
    int enable_renegotiation;
    int enable_early_data;
    int hsts_max_age;
    int hsts_include_subdomains;
    char alpn_protocols[AIONIC_TLS_ALPN_STRING_LEN];
} TLSConfig;

typedef struct TLSCtx TLSCtx;
typedef struct TLSConn TLSConn;

TLSCtx *tls_ctx_create(const TLSConfig *cfg);
void tls_ctx_destroy(TLSCtx *ctx);
int tls_ctx_reload_cert(TLSCtx *ctx, const char *cert_file, const char *key_file);
int tls_ctx_ocsp_update(TLSCtx *ctx);

TLSConn *tls_conn_new(TLSCtx *ctx, int fd);
void tls_conn_destroy(TLSConn *conn);
int tls_conn_accept(TLSConn *conn, int timeout_ms);
int tls_conn_read(TLSConn *conn, char *buf, size_t len, int timeout_ms);
int tls_conn_write(TLSConn *conn, const char *buf, size_t len, int timeout_ms);
int tls_conn_pending(TLSConn *conn);
int tls_conn_get_fd(const TLSConn *conn);
const char *tls_conn_get_cipher(const TLSConn *conn);
const char *tls_conn_get_version(const TLSConn *conn);
const char *tls_conn_get_servername(const TLSConn *conn);
int tls_conn_is_ocsp_stapled(const TLSConn *conn);
int tls_conn_set_fd(TLSConn *conn, int fd);
int tls_conn_shutdown(TLSConn *conn);
int tls_conn_close(TLSConn *conn);
int tls_conn_get_alpn(TLSConn *conn, char *buf, size_t len);

void tls_global_init(void);
void tls_global_cleanup(void);
const char *tls_error_string(TLSError err);

#endif
