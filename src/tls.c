#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include "tls.h"
#include "utils.h"

struct TLSConn {
    SSL *ssl;
    BIO *rbio;
    BIO *wbio;
    int fd;
    int handshake_done;
    int ocsp_stapled;
    char cipher[64];
    char version[32];
    char servername[256];
    uint64_t last_activity_ns;
};

struct TLSCtx {
    SSL_CTX *ssl_ctx;
    TLSConfig config;
    pthread_mutex_t ocsp_mutex;
    unsigned char *ocsp_response;
    size_t ocsp_response_len;
    time_t ocsp_last_update;
    X509 *cert;
    char alpn_protocols[AIONIC_TLS_ALPN_STRING_LEN];
    size_t alpn_len;
};

static pthread_once_t tls_global_once = PTHREAD_ONCE_INIT;
static int tls_global_initialized = 0;

#define TLS_DEBUG(...) fprintf(stderr, "[TLS] " __VA_ARGS__)

static void tls_do_global_init(void) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ERR_load_crypto_strings();
    tls_global_initialized = 1;
}

static int build_alpn_string(const char *protos, char *out, size_t out_len) {
    if (!protos || !*protos) {
        if (out_len < 16) return 0;
        out[0] = 8; out[1] = 'h'; out[2] = 't'; out[3] = 't'; out[4] = 'p'; out[5] = '/'; out[6] = '1'; out[7] = '.';
        out[8] = '1';
        return 8;
    }
    size_t pos = 0;
    const char *p = protos;
    while (*p && pos < out_len - 3) {
        const char *comma = strchr(p, ',');
        size_t plen = comma ? (size_t)(comma - p) : strlen(p);
        if (plen < 1 || plen > 255) { p = comma ? comma + 1 : p + plen; continue; }
        if (pos + plen + 1 > out_len) break;
        out[pos++] = (unsigned char)plen;
        memcpy(out + pos, p, plen);
        pos += plen;
        p = comma ? comma + 1 : p + plen;
        if (!comma) break;
    }
    return (int)pos;
}

static int alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                          const unsigned char *in, unsigned int inlen, void *arg) {
    (void)ssl; (void)arg;
    unsigned int i = 0;
    while (i < inlen) {
        unsigned char len = in[i++];
        if (i + len > inlen) break;
        if (len == 8 && memcmp(&in[i], "http/1.1", 8) == 0) {
            *out = &in[i];
            *outlen = 8;
            return SSL_TLSEXT_ERR_OK;
        }
        i += len;
    }
    i = 0;
    while (i < inlen) {
        unsigned char len = in[i++];
        if (i + len > inlen) break;
        if (len == 2 && memcmp(&in[i], "h2", 2) == 0) {
            *out = &in[i];
            *outlen = 2;
            return SSL_TLSEXT_ERR_OK;
        }
        i += len;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

static int ocsp_resp_cb(SSL *ssl, void *arg) {
    (void)arg;
    TLSCtx *ctx = (TLSCtx *)SSL_get_SSL_CTX(ssl);
    if (!ctx || !ctx->ocsp_response || ctx->ocsp_response_len == 0) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    TLSConn *conn = (TLSConn *)SSL_get_app_data(ssl);
    if (conn) conn->ocsp_stapled = 1;
    unsigned char *resp = OPENSSL_malloc(ctx->ocsp_response_len);
    if (!resp) return SSL_TLSEXT_ERR_NOACK;
    memcpy(resp, ctx->ocsp_response, ctx->ocsp_response_len);
    SSL_set_tlsext_status_ocsp_resp(ssl, resp, (long)ctx->ocsp_response_len);
    return SSL_TLSEXT_ERR_OK;
}

static int ocsp_update_internal(TLSCtx *ctx) {
    if (!ctx->cert) return -1;
    STACK_OF(X509) *chain = NULL;
    SSL_CTX_get0_chain_certs(ctx->ssl_ctx, &chain);
    if (!chain || sk_X509_num(chain) < 1) return -1;
    X509 *issuer = sk_X509_value(chain, 0);
    if (!issuer) return -1;
    OCSP_CERTID *cert_id = OCSP_cert_to_id(NULL, ctx->cert, issuer);
    if (!cert_id) return -1;
    const char *ocsp_url = NULL;
    AUTHORITY_INFO_ACCESS *info = X509_get_ext_d2i(ctx->cert, NID_info_access, NULL, NULL);
    if (info) {
        for (int i = 0; i < sk_ACCESS_DESCRIPTION_num(info); i++) {
            ACCESS_DESCRIPTION *ad = sk_ACCESS_DESCRIPTION_value(info, i);
            if (OBJ_obj2nid(ad->method) == NID_ad_OCSP && ad->location->type == GEN_URI) {
                ocsp_url = (const char *)ASN1_STRING_get0_data(ad->location->d.ia5);
                break;
            }
        }
        AUTHORITY_INFO_ACCESS_free(info);
    }
    if (!ocsp_url) { OCSP_CERTID_free(cert_id); return -1; }
    BIO *bio = BIO_new_connect(ocsp_url);
    if (!bio) { OCSP_CERTID_free(cert_id); return -1; }
    OCSP_REQUEST *req = OCSP_REQUEST_new();
    if (!req) { BIO_free_all(bio); OCSP_CERTID_free(cert_id); return -1; }
    OCSP_request_add0_id(req, cert_id);
    OCSP_RESPONSE *resp = OCSP_sendreq_bio(bio, NULL, req);
    BIO_free_all(bio);
    if (!resp) { OCSP_REQUEST_free(req); return -1; }
    const OCSP_BASICRESP *basic = OCSP_response_get1_basic(resp);
    if (!basic) { OCSP_RESPONSE_free(resp); OCSP_REQUEST_free(req); return -1; }
    free(ctx->ocsp_response);
    ctx->ocsp_response = NULL;
    ctx->ocsp_response_len = 0;
    unsigned char *resp_data = NULL;
    int resp_len = i2d_OCSP_RESPONSE(resp, &resp_data);
    if (resp_len > 0) {
        ctx->ocsp_response = malloc((size_t)resp_len);
        if (ctx->ocsp_response) {
            memcpy(ctx->ocsp_response, resp_data, (size_t)resp_len);
            ctx->ocsp_response_len = (size_t)resp_len;
            ctx->ocsp_last_update = time(NULL);
        }
        OPENSSL_free(resp_data);
    }
    OCSP_BASICRESP_free((OCSP_BASICRESP *)basic);
    OCSP_RESPONSE_free(resp);
    OCSP_REQUEST_free(req);
    return ctx->ocsp_response ? 0 : -1;
}

TLSCtx *tls_ctx_create(const TLSConfig *cfg) {
    TLSCtx *ctx = calloc(1, sizeof(TLSCtx));
    if (!ctx) return NULL;
    memcpy(&ctx->config, cfg, sizeof(TLSConfig));
    pthread_mutex_init(&ctx->ocsp_mutex, NULL);
    if (!cfg->cert_file[0] || !cfg->key_file[0]) {
        TLS_DEBUG("No certificate or key file configured\n");
        goto error;
    }
    ctx->ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx->ssl_ctx) { TLS_DEBUG("SSL_CTX_new failed\n"); goto error; }
    SSL_CTX_set_min_proto_version(ctx->ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx->ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_mode(ctx->ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_RELEASE_BUFFERS);
    if (SSL_CTX_use_certificate_chain_file(ctx->ssl_ctx, cfg->cert_file) <= 0) {
        TLS_DEBUG("Failed to load certificate: %s\n", cfg->cert_file);
        goto error;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, cfg->key_file, SSL_FILETYPE_PEM) <= 0) {
        TLS_DEBUG("Failed to load private key: %s\n", cfg->key_file);
        goto error;
    }
    if (!SSL_CTX_check_private_key(ctx->ssl_ctx)) {
        TLS_DEBUG("Private key does not match certificate\n");
        goto error;
    }
    ctx->cert = SSL_CTX_get0_certificate(ctx->ssl_ctx);
    if (ctx->cert) X509_up_ref(ctx->cert);
    if (cfg->ca_file[0]) {
        if (!SSL_CTX_load_verify_locations(ctx->ssl_ctx, cfg->ca_file, cfg->ca_path[0] ? cfg->ca_path : NULL)) {
            TLS_DEBUG("Failed to load CA file\n");
        }
        if (cfg->verify_client) {
            SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
            SSL_CTX_set_verify_depth(ctx->ssl_ctx, cfg->verify_depth > 0 ? cfg->verify_depth : 10);
        }
    }
    const char *cipher_list = cfg->cipher_list[0] ? cfg->cipher_list :
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384";
    if (!SSL_CTX_set_cipher_list(ctx->ssl_ctx, cipher_list)) {
        TLS_DEBUG("Failed to set cipher list\n");
    }
    const char *ciphersuites_tls13 = cfg->cipher_suites_tls13[0] ? cfg->cipher_suites_tls13 :
        "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    if (!SSL_CTX_set_ciphersuites(ctx->ssl_ctx, ciphersuites_tls13)) {
        TLS_DEBUG("Failed to set TLS 1.3 ciphersuites\n");
    }
    if (cfg->prefer_server_ciphers) {
        SSL_CTX_set_options(ctx->ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
    }
    SSL_CTX_set_options(ctx->ssl_ctx,
        SSL_OP_NO_COMPRESSION |
        SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION |
        SSL_OP_NO_TICKET |
        SSL_OP_SINGLE_DH_USE |
        SSL_OP_SINGLE_ECDH_USE);
    if (!cfg->enable_renegotiation) {
        SSL_CTX_set_options(ctx->ssl_ctx, SSL_OP_NO_RENEGOTIATION);
    }
    SSL_CTX_set_session_cache_mode(ctx->ssl_ctx, SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_INTERNAL);
    SSL_CTX_sess_set_cache_size(ctx->ssl_ctx, (long)(cfg->session_cache_size > 0 ? (long)cfg->session_cache_size : AIONIC_TLS_SESSION_CACHE_SIZE));
    if (SSL_CTX_set1_groups_list(ctx->ssl_ctx, "P-256:P-384:P-521") != 1) {
        TLS_DEBUG("Failed to set ECDH groups\n");
    }
    ctx->alpn_len = (size_t)build_alpn_string(cfg->alpn_protocols, ctx->alpn_protocols, sizeof(ctx->alpn_protocols));
    SSL_CTX_set_alpn_select_cb(ctx->ssl_ctx, alpn_select_cb, ctx);
    SSL_CTX_set_alpn_protos(ctx->ssl_ctx, (const unsigned char *)ctx->alpn_protocols, (unsigned int)ctx->alpn_len);
    if (cfg->enable_ocsp_stapling) {
        SSL_CTX_set_tlsext_status_cb(ctx->ssl_ctx, ocsp_resp_cb);
    }
    return ctx;
error:
    if (ctx->ssl_ctx) SSL_CTX_free(ctx->ssl_ctx);
    if (ctx->cert) X509_free(ctx->cert);
    pthread_mutex_destroy(&ctx->ocsp_mutex);
    free(ctx->ocsp_response);
    free(ctx);
    return NULL;
}

void tls_ctx_destroy(TLSCtx *ctx) {
    if (!ctx) return;
    if (ctx->ssl_ctx) SSL_CTX_free(ctx->ssl_ctx);
    if (ctx->cert) X509_free(ctx->cert);
    free(ctx->ocsp_response);
    pthread_mutex_destroy(&ctx->ocsp_mutex);
    free(ctx);
}

int tls_ctx_reload_cert(TLSCtx *ctx, const char *cert_file, const char *key_file) {
    if (!ctx || !ctx->ssl_ctx) return -1;
    SSL_CTX_use_certificate_chain_file(ctx->ssl_ctx, cert_file);
    if (SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, key_file, SSL_FILETYPE_PEM) <= 0) return -1;
    if (!SSL_CTX_check_private_key(ctx->ssl_ctx)) return -1;
    if (ctx->cert) X509_free(ctx->cert);
    ctx->cert = SSL_CTX_get0_certificate(ctx->ssl_ctx);
    if (ctx->cert) X509_up_ref(ctx->cert);
    return 0;
}

int tls_ctx_ocsp_update(TLSCtx *ctx) {
    if (!ctx) return -1;
    int ret;
    pthread_mutex_lock(&ctx->ocsp_mutex);
    ret = ocsp_update_internal(ctx);
    pthread_mutex_unlock(&ctx->ocsp_mutex);
    return ret;
}

TLSConn *tls_conn_new(TLSCtx *ctx, int fd) {
    if (!ctx || !ctx->ssl_ctx) return NULL;
    TLSConn *conn = calloc(1, sizeof(TLSConn));
    if (!conn) return NULL;
    conn->ssl = SSL_new(ctx->ssl_ctx);
    if (!conn->ssl) { free(conn); return NULL; }
    SSL_set_app_data(conn->ssl, conn);
    conn->rbio = BIO_new(BIO_s_mem());
    conn->wbio = BIO_new(BIO_s_mem());
    if (!conn->rbio || !conn->wbio) {
        if (conn->rbio) BIO_free(conn->rbio);
        if (conn->wbio) BIO_free(conn->wbio);
        SSL_free(conn->ssl);
        free(conn);
        return NULL;
    }
    BIO_set_mem_eof_return(conn->rbio, -1);
    BIO_set_mem_eof_return(conn->wbio, -1);
    SSL_set_bio(conn->ssl, conn->rbio, conn->wbio);
    SSL_set_accept_state(conn->ssl);
    conn->fd = fd;
    conn->last_activity_ns = 0;
    return conn;
}

void tls_conn_destroy(TLSConn *conn) {
    if (!conn) return;
    if (conn->ssl) SSL_free(conn->ssl);
    memset(conn, 0, sizeof(TLSConn));
    free(conn);
}

static int tls_flush_write(TLSConn *conn) {
    if (!conn || conn->fd < 0) return TLS_ERR_CLOSED;
    int total = 0;
    for (;;) {
        char buf[65536];
        int n = BIO_read(conn->wbio, buf, (int)sizeof(buf));
        if (n <= 0) break;
        int off = 0;
        while (off < n) {
            int s = (int)send(conn->fd, buf + off, (size_t)(n - off), MSG_NOSIGNAL);
            if (s <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    struct pollfd pfd;
                    pfd.fd = conn->fd;
                    pfd.events = POLLOUT;
                    poll(&pfd, 1, 10);
                    continue;
                }
                return TLS_ERR_SYSTEM;
            }
            off += s;
            total += s;
        }
    }
    return total;
}

int tls_conn_accept(TLSConn *conn, int timeout_ms) {
    (void)timeout_ms;
    if (!conn || !conn->ssl) return TLS_ERR_CLOSED;
    if (conn->handshake_done) return TLS_OK;

    for (;;) {
        char enc_buf[65536];
        int n = (int)recv(conn->fd, enc_buf, sizeof(enc_buf), MSG_DONTWAIT);
        if (n > 0) {
            BIO_write(conn->rbio, enc_buf, n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        } else if (n == 0) {
            return TLS_ERR_CLOSED;
        } else {
            return TLS_ERR_SYSTEM;
        }

        int ret = SSL_accept(conn->ssl);
        int ssl_err = SSL_get_error(conn->ssl, ret);
        if (ret == 1) {
            conn->handshake_done = 1;
            const SSL_CIPHER *cipher = SSL_get_current_cipher(conn->ssl);
            if (cipher) {
                snprintf(conn->cipher, sizeof(conn->cipher), "%s", SSL_CIPHER_get_name(cipher));
            }
            snprintf(conn->version, sizeof(conn->version), "%s", SSL_get_version(conn->ssl));
            const char *sni = SSL_get_servername(conn->ssl, TLSEXT_NAMETYPE_host_name);
            if (sni) snprintf(conn->servername, sizeof(conn->servername), "%s", sni);
            tls_flush_write(conn);
            return TLS_OK;
        }
        if (ssl_err == SSL_ERROR_WANT_READ) {
            tls_flush_write(conn);
            if (n <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return TLS_ERR_WANT_READ;
            }
            continue;
        }
        if (ssl_err == SSL_ERROR_WANT_WRITE) {
            tls_flush_write(conn);
            return TLS_ERR_WANT_WRITE;
        }
        if (ssl_err == SSL_ERROR_SYSCALL || ssl_err == SSL_ERROR_SSL) {
            return TLS_ERR_SYSTEM;
        }
        if (ssl_err == SSL_ERROR_ZERO_RETURN) return TLS_ERR_CLOSED;
        return TLS_ERR_SYSTEM;
    }
}

int tls_conn_read(TLSConn *conn, char *buf, size_t len, int timeout_ms) {
    (void)timeout_ms;
    if (!conn || !conn->ssl) return TLS_ERR_CLOSED;
    if (conn->fd < 0) return TLS_ERR_CLOSED;
    if (!conn->handshake_done) {
        int ret = tls_conn_accept(conn, timeout_ms);
        if (ret != TLS_OK) return ret;
        if (!conn->handshake_done) return TLS_ERR_WANT_READ;
    }
    for (;;) {
        char enc_buf[65536];
        int got_data = 0;
        for (;;) {
            int n = (int)recv(conn->fd, enc_buf, sizeof(enc_buf), MSG_DONTWAIT);
            if (n > 0) {
                BIO_write(conn->rbio, enc_buf, n);
                got_data = 1;
            } else if (n == 0) {
                return TLS_ERR_CLOSED;
            } else {
                break;
            }
        }
        int ret = SSL_read(conn->ssl, buf, (int)len);
        if (ret > 0) { tls_flush_write(conn); return ret; }
        int ssl_err = SSL_get_error(conn->ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            tls_flush_write(conn);
            if (got_data) continue;
            return TLS_ERR_WANT_READ;
        }
        if (ssl_err == SSL_ERROR_WANT_WRITE) { tls_flush_write(conn); return TLS_ERR_WANT_WRITE; }
        if (ssl_err == SSL_ERROR_ZERO_RETURN) return TLS_ERR_CLOSED;
        return TLS_ERR_SYSTEM;
    }
}

int tls_conn_write(TLSConn *conn, const char *buf, size_t len, int timeout_ms) {
    (void)timeout_ms;
    if (!conn || !conn->ssl) return TLS_ERR_CLOSED;
    if (conn->fd < 0) return TLS_ERR_CLOSED;
    if (!conn->handshake_done) {
        int ret = tls_conn_accept(conn, timeout_ms);
        if (ret != TLS_OK) return ret;
        if (!conn->handshake_done) return TLS_ERR_WANT_WRITE;
    }
    int ret = SSL_write(conn->ssl, buf, (int)len);
    if (ret > 0) { tls_flush_write(conn); return ret; }
    int ssl_err = SSL_get_error(conn->ssl, ret);
    if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
        tls_flush_write(conn);
        return TLS_ERR_WANT_WRITE;
    }
    if (ssl_err == SSL_ERROR_ZERO_RETURN) return TLS_ERR_CLOSED;
    return TLS_ERR_SYSTEM;
}

int tls_conn_pending(TLSConn *conn) {
    if (!conn || !conn->ssl) return 0;
    return SSL_pending(conn->ssl);
}

int tls_conn_get_fd(const TLSConn *conn) {
    return conn ? conn->fd : -1;
}

const char *tls_conn_get_cipher(const TLSConn *conn) {
    return conn ? conn->cipher : NULL;
}

const char *tls_conn_get_version(const TLSConn *conn) {
    return conn ? conn->version : NULL;
}

const char *tls_conn_get_servername(const TLSConn *conn) {
    return conn ? conn->servername : NULL;
}

int tls_conn_is_ocsp_stapled(const TLSConn *conn) {
    return conn ? conn->ocsp_stapled : 0;
}

int tls_conn_set_fd(TLSConn *conn, int fd) {
    if (!conn) return -1;
    conn->fd = fd;
    return 0;
}

int tls_conn_get_alpn(TLSConn *conn, char *buf, size_t len) {
    if (!conn || !conn->ssl || !buf || len == 0) return -1;
    const unsigned char *alpn;
    unsigned int alpn_len;
    SSL_get0_alpn_selected(conn->ssl, &alpn, &alpn_len);
    if (!alpn || alpn_len == 0) {
        buf[0] = '\0';
        return -1;
    }
    size_t copy_len = (size_t)alpn_len < len - 1 ? (size_t)alpn_len : len - 1;
    memcpy(buf, alpn, copy_len);
    buf[copy_len] = '\0';
    return 0;
}

int tls_conn_shutdown(TLSConn *conn) {
    if (!conn || !conn->ssl) return -1;
    SSL_shutdown(conn->ssl);
    tls_flush_write(conn);
    conn->handshake_done = 0;
    return 0;
}

int tls_conn_close(TLSConn *conn) {
    if (!conn) return -1;
    tls_conn_shutdown(conn);
    if (conn->fd >= 0) { close(conn->fd); conn->fd = -1; }
    return 0;
}

void tls_global_init(void) {
    pthread_once(&tls_global_once, tls_do_global_init);
}

void tls_global_cleanup(void) {
    if (tls_global_initialized) {
        EVP_cleanup();
        ERR_free_strings();
        CRYPTO_cleanup_all_ex_data();
        tls_global_initialized = 0;
    }
}

const char *tls_error_string(TLSError err) {
    switch (err) {
        case TLS_OK: return "success";
        case TLS_ERR_WANT_READ: return "want read";
        case TLS_ERR_WANT_WRITE: return "want write";
        case TLS_ERR_WANT_CONNECT: return "want connect";
        case TLS_ERR_WANT_ACCEPT: return "want accept";
        case TLS_ERR_CLOSED: return "connection closed";
        case TLS_ERR_SYSTEM: return "system error";
        case TLS_ERR_MEMORY: return "memory error";
        case TLS_ERR_CERT: return "certificate error";
        case TLS_ERR_VERIFY: return "verification error";
        case TLS_ERR_OCSP: return "OCSP error";
        default: return "unknown error";
    }
}
