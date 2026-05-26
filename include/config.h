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

    int enable_iouring;
    int enable_zero_copy;
    int enable_ratelimiter;
    int rate_limit_rps;
    int enable_observability;
    int enable_streaming;
    int enable_smart_routing;
    int enable_keepalive;
    int keepalive_timeout;
    int worker_threads;
} Config;

int load_config(const char *filename, Config *config);
void free_config(Config *config);

#endif
