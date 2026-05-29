#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "config.h"
#include "utils.h"

static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

static int parse_config_line(const char *line, Config *config) {
    char *line_copy = strdup(line);
    if (!line_copy) return -1;
    char *key = line_copy;
    char *value = strchr(key, '=');
    if (!value) { free(line_copy); return -1; }
    *value = '\0'; value++;
    key = trim_whitespace(key);
    value = trim_whitespace(value);

    if (strcmp(key, "port") == 0) config->port = atoi(value);
    else if (strcmp(key, "tls_port") == 0) config->tls_port = atoi(value);
    else if (strcmp(key, "thread_count") == 0) config->thread_count = atoi(value);
    else if (strcmp(key, "max_connections") == 0) config->max_connections = atoi(value);
    else if (strcmp(key, "request_timeout") == 0) config->request_timeout = atoi(value);
    else if (strcmp(key, "buffer_size") == 0) config->buffer_size = atoi(value);
    else if (strcmp(key, "log_file") == 0) { free(config->log_file); config->log_file = strdup(value); }
    else if (strcmp(key, "enable_cache") == 0) config->enable_cache = atoi(value);
    else if (strcmp(key, "cache_size") == 0) config->cache_size = atoi(value);
    else if (strcmp(key, "cache_ttl") == 0) config->cache_ttl = atoi(value);
    else if (strcmp(key, "enable_firewall") == 0) config->enable_firewall = atoi(value);
    else if (strcmp(key, "enable_optimization") == 0) config->enable_optimization = atoi(value);
    else if (strcmp(key, "api_key") == 0) {
        if (config->api_key_count < 64) config->api_keys[config->api_key_count++] = strdup(value);
    } else if (strcmp(key, "max_request_size") == 0) config->max_request_size = atoi(value);
    else if (strcmp(key, "verify_ssl") == 0) config->verify_ssl = atoi(value);
    else if (strcmp(key, "enable_iouring") == 0) config->enable_iouring = atoi(value);
    else if (strcmp(key, "iouring_queue_depth") == 0) config->iouring_queue_depth = atoi(value);
    else if (strcmp(key, "iouring_sqpoll") == 0) config->iouring_sqpoll = atoi(value);
    else if (strcmp(key, "enable_zero_copy") == 0) config->enable_zero_copy = atoi(value);
    else if (strcmp(key, "enable_ratelimiter") == 0) config->enable_ratelimiter = atoi(value);
    else if (strcmp(key, "rate_limit_rps") == 0) config->rate_limit_rps = atoi(value);
    else if (strcmp(key, "enable_observability") == 0) config->enable_observability = atoi(value);
    else if (strcmp(key, "enable_streaming") == 0) config->enable_streaming = atoi(value);
    else if (strcmp(key, "enable_smart_routing") == 0) config->enable_smart_routing = atoi(value);
    else if (strcmp(key, "enable_keepalive") == 0) config->enable_keepalive = atoi(value);
    else if (strcmp(key, "keepalive_timeout") == 0) config->keepalive_timeout = atoi(value);
    else if (strcmp(key, "worker_threads") == 0) config->worker_threads = atoi(value);
    else if (strcmp(key, "enable_tls") == 0) config->enable_tls = atoi(value);
    else if (strcmp(key, "tls_cert_file") == 0) strncpy(config->tls_cert_file, value, sizeof(config->tls_cert_file) - 1);
    else if (strcmp(key, "tls_key_file") == 0) strncpy(config->tls_key_file, value, sizeof(config->tls_key_file) - 1);
    else if (strcmp(key, "tls_ca_file") == 0) strncpy(config->tls_ca_file, value, sizeof(config->tls_ca_file) - 1);
    else if (strcmp(key, "tls_ca_path") == 0) strncpy(config->tls_ca_path, value, sizeof(config->tls_ca_path) - 1);
    else if (strcmp(key, "tls_cipher_list") == 0) strncpy(config->tls_cipher_list, value, sizeof(config->tls_cipher_list) - 1);
    else if (strcmp(key, "tls_ciphersuites_tls13") == 0) strncpy(config->tls_ciphersuites_tls13, value, sizeof(config->tls_ciphersuites_tls13) - 1);
    else if (strcmp(key, "tls_min_version") == 0) config->tls_min_version = atoi(value);
    else if (strcmp(key, "tls_prefer_server_ciphers") == 0) config->tls_prefer_server_ciphers = atoi(value);
    else if (strcmp(key, "tls_verify_client") == 0) config->tls_verify_client = atoi(value);
    else if (strcmp(key, "tls_verify_depth") == 0) config->tls_verify_depth = atoi(value);
    else if (strcmp(key, "tls_enable_ocsp") == 0) config->tls_enable_ocsp = atoi(value);
    else if (strcmp(key, "tls_ocsp_refresh_interval") == 0) config->tls_ocsp_refresh_interval = atoi(value);
    else if (strcmp(key, "tls_session_cache_size") == 0) config->tls_session_cache_size = atoi(value);
    else if (strcmp(key, "tls_enable_renegotiation") == 0) config->tls_enable_renegotiation = atoi(value);
    else if (strcmp(key, "tls_enable_early_data") == 0) config->tls_enable_early_data = atoi(value);
    else if (strcmp(key, "tls_hsts_max_age") == 0) config->tls_hsts_max_age = atoi(value);
    else if (strcmp(key, "tls_hsts_include_subdomains") == 0) config->tls_hsts_include_subdomains = atoi(value);
    else if (strcmp(key, "tls_alpn_protocols") == 0) strncpy(config->tls_alpn_protocols, value, sizeof(config->tls_alpn_protocols) - 1);
    else if (strcmp(key, "enable_http2") == 0) config->enable_http2 = atoi(value);
    else if (strcmp(key, "http2_max_concurrent_streams") == 0) config->http2_max_concurrent_streams = atoi(value);
    else if (strcmp(key, "http2_max_header_list_size") == 0) config->http2_max_header_list_size = atoi(value);
    else if (strcmp(key, "http2_initial_window_size") == 0) config->http2_initial_window_size = atoi(value);
    else if (strcmp(key, "http2_max_frame_size") == 0) config->http2_max_frame_size = atoi(value);
    else if (strcmp(key, "http2_enable_push") == 0) config->http2_enable_push = atoi(value);
    else if (strcmp(key, "graceful_shutdown_timeout") == 0) config->graceful_shutdown_timeout = atoi(value);
    else if (strcmp(key, "ai_model") == 0) {
        AIModelConfig *new_models = realloc(config->ai_models, sizeof(AIModelConfig) * (config->ai_model_count + 1));
        if (new_models) {
            config->ai_models = new_models;
            AIModelConfig *m = &config->ai_models[config->ai_model_count];
            memset(m, 0, sizeof(AIModelConfig));
            char *val_copy = strdup(value);
            char *p = val_copy, *next;
            next = strchr(p, '|'); if (next) { *next = '\0'; m->name = strdup(p); p = next + 1; }
            next = strchr(p, '|'); if (next) { *next = '\0'; m->api_endpoint = strdup(p); p = next + 1; }
            next = strchr(p, '|'); if (next) { *next = '\0'; m->api_key_env = strdup(p); p = next + 1; }
            next = strchr(p, '|'); if (next) { *next = '\0'; m->max_tokens = atoi(p); p = next + 1; }
            m->temperature = atof(p);
            char *tsep = strchr(p, '|');
            if (tsep) {
                p = tsep + 1;
                m->tier = atoi(p);
            } else {
                m->tier = 2;
            }
            m->is_available = 1;
            m->provider[0] = '\0';
            if (m->api_endpoint) {
                if (strstr(m->api_endpoint, "api.groq.com")) snprintf(m->provider, sizeof(m->provider), "groq");
                else if (strstr(m->api_endpoint, "api.openai.com")) snprintf(m->provider, sizeof(m->provider), "openai");
                else if (strstr(m->api_endpoint, "api.anthropic.com")) snprintf(m->provider, sizeof(m->provider), "anthropic");
                else if (strstr(m->api_endpoint, "generativelanguage")) snprintf(m->provider, sizeof(m->provider), "gemini");
                else if (strstr(m->api_endpoint, "api.deepseek.com")) snprintf(m->provider, sizeof(m->provider), "deepseek");
                else if (strstr(m->api_endpoint, "api.moonshot.cn")) snprintf(m->provider, sizeof(m->provider), "moonshot");
                else if (strstr(m->api_endpoint, "open.bigmodel.cn")) snprintf(m->provider, sizeof(m->provider), "zhipu");
                else if (strstr(m->api_endpoint, "api.perplexity.ai")) snprintf(m->provider, sizeof(m->provider), "perplexity");
                else if (strstr(m->api_endpoint, "api.mistral.ai")) snprintf(m->provider, sizeof(m->provider), "mistral");
                else if (strstr(m->api_endpoint, "localhost")) snprintf(m->provider, sizeof(m->provider), "local");
                else snprintf(m->provider, sizeof(m->provider), "unknown");
            }
            config->ai_model_count++;
            free(val_copy);
        }
    }
    free(line_copy);
    return 0;
}

int load_config(const char *filename, Config *config) {
    if (!filename || !config) return -1;
    memset(config, 0, sizeof(Config));
    config->port = 8080;
    config->tls_port = 8443;
    config->thread_count = 4;
    config->max_connections = 1024;
    config->request_timeout = 30000;
    config->buffer_size = 8192;
    config->log_file = NULL;
    config->enable_cache = 1;
    config->cache_size = 1000;
    config->cache_ttl = 3600;
    config->enable_firewall = 1;
    config->enable_optimization = 1;
    config->api_key_count = 0;
    config->max_request_size = 33554432;
    config->verify_ssl = 1;
    config->ai_models = NULL;
    config->ai_model_count = 0;
    config->enable_iouring = 1;
    config->iouring_queue_depth = 512;
    config->iouring_sqpoll = 0;
    config->enable_zero_copy = 1;
    config->enable_ratelimiter = 0;
    config->rate_limit_rps = 100;
    config->enable_observability = 1;
    config->enable_streaming = 1;
    config->enable_smart_routing = 1;
    config->enable_keepalive = 1;
    config->keepalive_timeout = 30;
    config->worker_threads = 4;
    config->enable_tls = 0;
    config->tls_min_version = 2;
    config->tls_prefer_server_ciphers = 1;
    config->tls_verify_client = 0;
    config->tls_verify_depth = 10;
    config->tls_enable_ocsp = 0;
    config->tls_ocsp_refresh_interval = 3600;
    config->tls_session_cache_size = 2048;
    config->tls_enable_renegotiation = 0;
    config->tls_enable_early_data = 0;
    config->tls_hsts_max_age = 31536000;
    config->tls_hsts_include_subdomains = 1;
    config->enable_http2 = 1;
    config->http2_max_concurrent_streams = 256;
    config->http2_max_header_list_size = 65536;
    config->http2_initial_window_size = 65535;
    config->http2_max_frame_size = 16384;
    config->http2_enable_push = 0;
    config->graceful_shutdown_timeout = 30;

    char *file_content = read_file(filename);
    if (!file_content) return -1;
    char *line = strtok(file_content, "\n");
    while (line) {
        size_t l = strlen(line);
        if (l > 0 && line[0] != '#') parse_config_line(line, config);
        line = strtok(NULL, "\n");
    }
    free(file_content);
    return 0;
}

void free_config(Config *config) {
    if (!config) return;
    free(config->log_file);
    for (int i = 0; i < config->api_key_count; i++) free(config->api_keys[i]);
    for (int i = 0; i < config->ai_model_count; i++) {
        free(config->ai_models[i].name);
        free(config->ai_models[i].api_endpoint);
        free(config->ai_models[i].api_key_env);
    }
    free(config->ai_models);
    memset(config, 0, sizeof(Config));
}
