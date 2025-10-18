#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// ===== Project Headers =====
#include "firewall.h"
#include "utils.h"


typedef struct {
    FirewallEntry *entries;
    int entry_count;
    int entry_capacity;
    pthread_mutex_t mutex;
    char **allowed_api_keys;
    int api_key_count;
    int max_requests_per_minute;
    int block_duration_minutes;
} Firewall;

static Firewall global_firewall;

static FirewallEntry *find_entry(const char *ip_address) {
    for (int i = 0; i < global_firewall.entry_count; i++) {
        if (strcmp(global_firewall.entries[i].ip_address, ip_address) == 0) {
            return &global_firewall.entries[i];
        }
    }
    return NULL;
}

static FirewallEntry *add_entry(const char *ip_address) {
    if (global_firewall.entry_count >= global_firewall.entry_capacity) {
        int new_capacity = global_firewall.entry_capacity * 2;
        FirewallEntry *new_entries = realloc(global_firewall.entries, 
                                           sizeof(FirewallEntry) * new_capacity);
        if (!new_entries) {
            return NULL;
        }
        
        global_firewall.entries = new_entries;
        global_firewall.entry_capacity = new_capacity;
    }
    
    FirewallEntry *entry = &global_firewall.entries[global_firewall.entry_count];
    entry->ip_address = strdup(ip_address);
    entry->request_count = 0;
    entry->last_request = time(NULL);
    entry->is_blocked = 0;
    
    global_firewall.entry_count++;
    return entry;
}

int firewall_init(const Config *config) {
    global_firewall.entry_capacity = 1024;
    global_firewall.entries = calloc(global_firewall.entry_capacity, sizeof(FirewallEntry));
    if (!global_firewall.entries) {
        return -1;
    }
    
    global_firewall.entry_count = 0;
    global_firewall.allowed_api_keys = NULL;
    global_firewall.api_key_count = 0;
    global_firewall.max_requests_per_minute = 60; 
    global_firewall.block_duration_minutes = 5;   
    
    if (pthread_mutex_init(&global_firewall.mutex, NULL) != 0) {
        free(global_firewall.entries);
        return -1;
    }
    
    if (config && config->api_key_count > 0) {
        global_firewall.allowed_api_keys = malloc(sizeof(char *) * config->api_key_count);
        if (!global_firewall.allowed_api_keys) {
            free(global_firewall.entries);
            pthread_mutex_destroy(&global_firewall.mutex);
            return -1;
        }
        
        for (int i = 0; i < config->api_key_count; i++) {
            global_firewall.allowed_api_keys[i] = strdup(config->api_keys[i]);
        }
        
        global_firewall.api_key_count = config->api_key_count;
    }
    
    log_message("FIREWALL", "Firewall initialized");
    return 0;
}

int firewall_check_request(const char *ip_address, const char *api_key) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    FirewallEntry *entry = find_entry(ip_address);
    if (!entry) {
        entry = add_entry(ip_address);
        if (!entry) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
    }
    
    if (entry->is_blocked) {
        time_t current_time = time(NULL);
        if (current_time - entry->last_request >= global_firewall.block_duration_minutes * 60) {
            entry->is_blocked = 0;
            entry->request_count = 0;
        } else {
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;  
        }
    }
    
    if (global_firewall.api_key_count > 0) {
        if (!api_key) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
        
        int api_key_valid = 0;
        for (int i = 0; i < global_firewall.api_key_count; i++) {
            if (strcmp(global_firewall.allowed_api_keys[i], api_key) == 0) {
                api_key_valid = 1;
                break;
            }
        }
        
        if (!api_key_valid) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
    }
    
    entry->request_count++;
    entry->last_request = time(NULL);
    
    if (entry->request_count > global_firewall.max_requests_per_minute) {
        entry->is_blocked = 1;
        
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "IP blocked due to rate limit: %s", ip_address);
        log_message("FIREWALL", log_msg);
        
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_block_ip(const char *ip_address) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    FirewallEntry *entry = find_entry(ip_address);
    if (!entry) {
        entry = add_entry(ip_address);
        if (!entry) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
    }
    
    entry->is_blocked = 1;
    entry->last_request = time(NULL);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "IP manually blocked: %s", ip_address);
    log_message("FIREWALL", log_msg);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_unblock_ip(const char *ip_address) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    FirewallEntry *entry = find_entry(ip_address);
    if (entry) {
        entry->is_blocked = 0;
        entry->request_count = 0;
        
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "IP unblocked: %s", ip_address);
        log_message("FIREWALL", log_msg);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_get_blocked_ips(char ***blocked_ips, int *count) {
    pthread_mutex_lock(&global_firewall.mutex);
    
    int blocked_count = 0;
    for (int i = 0; i < global_firewall.entry_count; i++) {
        if (global_firewall.entries[i].is_blocked) {
            blocked_count++;
        }
    }
    
    *count = blocked_count;
    *blocked_ips = malloc(sizeof(char *) * blocked_count);
    
    int index = 0;
    for (int i = 0; i < global_firewall.entry_count; i++) {
        if (global_firewall.entries[i].is_blocked) {
            (*blocked_ips)[index] = strdup(global_firewall.entries[i].ip_address);
            index++;
        }
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

void firewall_cleanup() {
    pthread_mutex_lock(&global_firewall.mutex);
    
    for (int i = 0; i < global_firewall.entry_count; i++) {
        free(global_firewall.entries[i].ip_address);
    }
    
    free(global_firewall.entries);
    
    for (int i = 0; i < global_firewall.api_key_count; i++) {
        free(global_firewall.allowed_api_keys[i]);
    }
    
    free(global_firewall.allowed_api_keys);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    pthread_mutex_destroy(&global_firewall.mutex);
    
    log_message("FIREWALL", "Firewall cleaned up");
}
