#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

// ===== Project Headers =====
#include "config.h"
#include "utils.h"


static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    
    char *end;
    
    while (isspace((unsigned char)*str)) {
        str++;
    }
    
    if (*str == '\0') {
        return str;
    }
    
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    
    *(end + 1) = '\0';
    
    return str;
}

static int parse_config_line(const char *line, Config *config) {
    char *line_copy = strdup(line);
    if (!line_copy) return -1;
    
    char *key = line_copy;
    char *value = strchr(key, '=');
    
    if (!value) {
        free(line_copy);
        return -1;
    }
    
    *value = '\0';
    value++;
    
    key = trim_whitespace(key);
    value = trim_whitespace(value);
    
    if (strcmp(key, "port") == 0) {
        config->port = atoi(value);
    } else if (strcmp(key, "thread_count") == 0) {
        config->thread_count = atoi(value);
    } else if (strcmp(key, "max_connections") == 0) {
        config->max_connections = atoi(value);
    } else if (strcmp(key, "request_timeout") == 0) {
        config->request_timeout = atoi(value);
    } else if (strcmp(key, "buffer_size") == 0) {
        config->buffer_size = atoi(value);
    } else if (strcmp(key, "log_file") == 0) {
        if (config->log_file) free(config->log_file);
        config->log_file = strdup(value);
    } else if (strcmp(key, "enable_cache") == 0) {
        config->enable_cache = atoi(value);
    } else if (strcmp(key, "cache_size") == 0) {
        config->cache_size = atoi(value);
    } else if (strcmp(key, "cache_ttl") == 0) {
        config->cache_ttl = atoi(value);
    } else if (strcmp(key, "enable_firewall") == 0) {
        config->enable_firewall = atoi(value);
    } else if (strcmp(key, "enable_optimization") == 0) {
        config->enable_optimization = atoi(value);
    } else if (strcmp(key, "api_key") == 0) {
        if (config->api_key_count < 64) {
            config->api_keys[config->api_key_count] = strdup(value);
            config->api_key_count++;
        }
    }
    
    free(line_copy);
    return 0;
}

int load_config(const char *filename, Config *config) {
    if (!filename || !config) {
        return -1;
    }
    
    memset(config, 0, sizeof(Config));
    config->port = 8080;
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
    
    char *file_content = read_file(filename);
    if (!file_content) {
        return -1;
    }
    
    char *line = strtok(file_content, "\n");
    while (line) {
        if (strlen(line) > 0 && line[0] != '#') {
            parse_config_line(line, config);
        }
        line = strtok(NULL, "\n");
    }
    
    free(file_content);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Configuration loaded from %s", filename);
    log_message("CONFIG", log_msg);
    
    return 0;
}

void free_config(Config *config) {
    if (!config) {
        return;
    }
    
    if (config->log_file) {
        free(config->log_file);
    }
    
    for (int i = 0; i < config->api_key_count; i++) {
        if (config->api_keys[i]) {
            free(config->api_keys[i]);
        }
    }
    
    memset(config, 0, sizeof(Config));
}
