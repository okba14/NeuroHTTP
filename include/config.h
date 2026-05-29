#ifndef AIONIC_CONFIG_H
#define AIONIC_CONFIG_H

typedef struct {
    char *name;
    char *api_endpoint;
    char *api_key_env;
    int max_tokens;
    float temperature;
    int is_available;
    int tier;
    char provider[32];
} AIModelConfig;

typedef struct Config {
    int port;
    int tls_port;
    int thread_count;
    int max_connections;
    int request_timeout;
    int buffer_size;
    char *log_file;
    char *api_keys[64];
    int api_key_count;
    int enable_cache;
    int cache_size;
    int cache_ttl;
    int enable_firewall;
    int enable_optimization;
    int max_request_size;
    int verify_ssl;
    AIModelConfig *ai_models;
    int ai_model_count;

    /* Engine options */
    int enable_iouring;
    int iouring_queue_depth;
    int iouring_sqpoll;
    int enable_zero_copy;
    int enable_ratelimiter;
    int rate_limit_rps;
    int enable_observability;
    int enable_streaming;
    int enable_smart_routing;
    int enable_keepalive;
    int keepalive_timeout;
    int worker_threads;

    /* TLS options */
    int enable_tls;
    char tls_cert_file[4096];
    char tls_key_file[4096];
    char tls_ca_file[4096];
    char tls_ca_path[4096];
    char tls_cipher_list[256];
    char tls_ciphersuites_tls13[256];
    int tls_min_version;
    int tls_prefer_server_ciphers;
    int tls_verify_client;
    int tls_verify_depth;
    int tls_enable_ocsp;
    int tls_ocsp_refresh_interval;
    int tls_session_cache_size;
    int tls_enable_renegotiation;
    int tls_enable_early_data;
    int tls_hsts_max_age;
    int tls_hsts_include_subdomains;
    char tls_alpn_protocols[128];

    /* HTTP/2 options */
    int enable_http2;
    int http2_max_concurrent_streams;
    int http2_max_header_list_size;
    int http2_initial_window_size;
    int http2_max_frame_size;
    int http2_enable_push;

    /* Graceful shutdown options */
    int graceful_shutdown_timeout;
} Config;

int load_config(const char *filename, Config *config);
void free_config(Config *config);

#endif
