#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <strings.h>

// ===== Project Headers =====
#include "config.h"
#include "firewall.h"
#include "utils.h"
#include "server.h"

// ===== Enhanced Firewall Structure =====
typedef struct {
    FirewallEntry *entries;
    int entry_count;
    int entry_capacity;
    pthread_mutex_t mutex;
    char **allowed_api_keys;
    int api_key_count;
    
    // New enhanced features
    AttackPattern *attack_patterns;
    int attack_pattern_count;
    WhitelistEntry *whitelist;
    int whitelist_count;
    BlacklistEntry *blacklist;
    int blacklist_count;
    
    // Configuration
    RateLimitConfig rate_limit_config;
    
    // State Control (Prevents Double Init)
    int is_initialized; 
    
    // Statistics
    FirewallStats stats;
} Firewall;

static Firewall global_firewall;

// ===== Static Helper Functions =====


static char *stristr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    
    char *h = (char *)haystack;
    char *n = (char *)needle;
    
    while (*h) {
        char *h_ptr = h;
        char *n_ptr = n;
        
        while (*h_ptr && *n_ptr && tolower(*h_ptr) == tolower(*n_ptr)) {
            h_ptr++;
            n_ptr++;
        }
        
        if (*n_ptr == '\0') return h;
        h++;
    }
    return NULL;
}


static void update_suspicion(FirewallEntry *entry, int score_add) {

    if (entry->suspicious_score > 0) {
        entry->suspicious_score = (int)(entry->suspicious_score * 0.95);
    }
    
    entry->suspicious_score += score_add;
}

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
    entry->block_start_time = 0;
    entry->suspicious_score = 0;
    
    global_firewall.entry_count++;
    global_firewall.stats.active_entries = global_firewall.entry_count;
    
    return entry;
}

static int is_ip_whitelisted(const char *ip_address) {
    for (int i = 0; i < global_firewall.whitelist_count; i++) {
        if (strcmp(global_firewall.whitelist[i].ip_address, ip_address) == 0) {
            if (global_firewall.whitelist[i].permanent || 
                global_firewall.whitelist[i].expiry_time > time(NULL)) {
                return 1;
            }
        }
    }
    return 0;
}

static int is_ip_blacklisted(const char *ip_address) {
    for (int i = 0; i < global_firewall.blacklist_count; i++) {
        if (strcmp(global_firewall.blacklist[i].ip_address, ip_address) == 0) {
            return 1;
        }
    }
    return 0;
}

static int detect_attack_pattern(const char *request_data) {
    if (!request_data || !global_firewall.attack_patterns) {
        return 0;
    }
    
    int max_severity = 0;
    for (int i = 0; i < global_firewall.attack_pattern_count; i++) {
        // Use stristr for case-insensitive matching (Bypasses Case variations)
        if (stristr(request_data, global_firewall.attack_patterns[i].pattern)) {
            global_firewall.attack_patterns[i].last_detected = time(NULL);
            global_firewall.stats.attack_pattern_hits++;
            
            if (global_firewall.attack_patterns[i].severity > max_severity) {
                max_severity = global_firewall.attack_patterns[i].severity;
            }
        }
    }
    return max_severity;
}

static int detect_brute_force(const char *ip_address) {
    time_t current_time = time(NULL);
    time_t window_start = current_time - global_firewall.rate_limit_config.brute_force_window_seconds;
    
    int failed_attempts = 0;
    for (int i = 0; i < global_firewall.entry_count; i++) {
        FirewallEntry *entry = &global_firewall.entries[i];
        if (strcmp(entry->ip_address, ip_address) == 0 && 
            entry->last_request >= window_start &&
            entry->is_blocked) {
            failed_attempts++;
        }
    }
    
    return failed_attempts >= global_firewall.rate_limit_config.brute_force_threshold;
}

static int get_requests_in_window(const char *ip_address, time_t window_seconds) {
    time_t current_time = time(NULL);
    time_t window_start = current_time - window_seconds;
    int requests_in_window = 0;
    
    for (int i = 0; i < global_firewall.entry_count; i++) {
        FirewallEntry *entry = &global_firewall.entries[i];
        if (strcmp(entry->ip_address, ip_address) == 0 && 
            entry->last_request >= window_start) {
            requests_in_window++;
        }
    }
    
    return requests_in_window;
}

// ===== Core Functions (Fixed Initialization) =====
int firewall_init(const struct Config *config) {
    // === IDEMPOTENCY CHECK (The fix for duplicate logs) ===
    if (global_firewall.is_initialized) {
        log_message("FIREWALL", "Firewall already initialized. Skipping duplicate init.");
        return 0;
    }

    pthread_mutex_lock(&global_firewall.mutex);

    // Initialize basic firewall
    global_firewall.entry_capacity = 1024;
    global_firewall.entries = calloc(global_firewall.entry_capacity, sizeof(FirewallEntry));
    if (!global_firewall.entries) {
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    
    global_firewall.entry_count = 0;
    global_firewall.allowed_api_keys = NULL;
    global_firewall.api_key_count = 0;
    
    // Initialize rate limiting with defaults
    global_firewall.rate_limit_config.max_requests_per_minute = DEFAULT_RATE_LIMIT_PER_MINUTE;
    global_firewall.rate_limit_config.block_duration_minutes = DEFAULT_BLOCK_DURATION_MINUTES;
    global_firewall.rate_limit_config.suspicious_threshold = DEFAULT_SUSPICIOUS_THRESHOLD;
    global_firewall.rate_limit_config.brute_force_threshold = DEFAULT_BRUTE_FORCE_THRESHOLD;
    global_firewall.rate_limit_config.brute_force_window_seconds = 300; // 5 minutes
    
    // Initialize enhanced features

    int initial_patterns = 25; // Estimate of patterns we add
    global_firewall.attack_patterns = calloc(initial_patterns, sizeof(AttackPattern));
    if (!global_firewall.attack_patterns) {
        free(global_firewall.entries);
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    global_firewall.attack_pattern_count = 0;

    global_firewall.whitelist = NULL;
    global_firewall.whitelist_count = 0;
    global_firewall.blacklist = NULL;
    global_firewall.blacklist_count = 0;
    
    // Initialize statistics
    memset(&global_firewall.stats, 0, sizeof(FirewallStats));
    global_firewall.stats.start_time = time(NULL);
    
    if (config && config->api_key_count > 0) {
        global_firewall.allowed_api_keys = malloc(sizeof(char *) * config->api_key_count);
        if (!global_firewall.allowed_api_keys) {
            free(global_firewall.entries);
            free(global_firewall.attack_patterns);
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
        
        for (int i = 0; i < config->api_key_count; i++) {
            global_firewall.allowed_api_keys[i] = strdup(config->api_keys[i]);
        }
        global_firewall.api_key_count = config->api_key_count;
    }
    
    // === SINGLE POINT OF TRUTH (Initial Attack Patterns) ===

    
    // SQL Injection
    strncpy(global_firewall.attack_patterns[0].pattern, " UNION ", 255); global_firewall.attack_patterns[0].severity = 8;
    strncpy(global_firewall.attack_patterns[1].pattern, " OR 1=1", 255); global_firewall.attack_patterns[1].severity = 9;
    strncpy(global_firewall.attack_patterns[2].pattern, " DROP TABLE", 255); global_firewall.attack_patterns[2].severity = 10;
    strncpy(global_firewall.attack_patterns[3].pattern, " SELECT * FROM", 255); global_firewall.attack_patterns[3].severity = 8;
    strncpy(global_firewall.attack_patterns[4].pattern, " INSERT INTO", 255); global_firewall.attack_patterns[4].severity = 7;
    strncpy(global_firewall.attack_patterns[5].pattern, " DELETE FROM", 255); global_firewall.attack_patterns[5].severity = 8;
    strncpy(global_firewall.attack_patterns[6].pattern, " UPDATE SET", 255); global_firewall.attack_patterns[6].severity = 7;
    strncpy(global_firewall.attack_patterns[7].pattern, " HAVING ", 255); global_firewall.attack_patterns[7].severity = 7;
    strncpy(global_firewall.attack_patterns[8].pattern, "--", 255); global_firewall.attack_patterns[8].severity = 5;

    // XSS (Cross Site Scripting) - Critical for WAF
    strncpy(global_firewall.attack_patterns[9].pattern, "<script", 255); global_firewall.attack_patterns[9].severity = 9;
    strncpy(global_firewall.attack_patterns[10].pattern, "javascript:", 255); global_firewall.attack_patterns[10].severity = 8;
    strncpy(global_firewall.attack_patterns[11].pattern, "onload=", 255); global_firewall.attack_patterns[11].severity = 8;
    strncpy(global_firewall.attack_patterns[12].pattern, "onerror=", 255); global_firewall.attack_patterns[12].severity = 8;
    strncpy(global_firewall.attack_patterns[13].pattern, "alert(", 255); global_firewall.attack_patterns[13].severity = 8;
    strncpy(global_firewall.attack_patterns[14].pattern, "document.cookie", 255); global_firewall.attack_patterns[14].severity = 8;
    strncpy(global_firewall.attack_patterns[15].pattern, "eval(", 255); global_firewall.attack_patterns[15].severity = 9;
    strncpy(global_firewall.attack_patterns[16].pattern, "iframe", 255); global_firewall.attack_patterns[16].severity = 7;
    strncpy(global_firewall.attack_patterns[17].pattern, "fromCharCode", 255); global_firewall.attack_patterns[17].severity = 8;

    // Path Traversal
    strncpy(global_firewall.attack_patterns[18].pattern, "../", 255); global_firewall.attack_patterns[18].severity = 5;
    strncpy(global_firewall.attack_patterns[19].pattern, "%2e%2e", 255); global_firewall.attack_patterns[19].severity = 5; // Encoded ..
    strncpy(global_firewall.attack_patterns[20].pattern, "..\\", 255); global_firewall.attack_patterns[20].severity = 5;

    // Common Bad User Agents (Scanners)
    strncpy(global_firewall.attack_patterns[21].pattern, "sqlmap", 255); global_firewall.attack_patterns[21].severity = 10;
    strncpy(global_firewall.attack_patterns[22].pattern, "nmap", 255); global_firewall.attack_patterns[22].severity = 10;
    strncpy(global_firewall.attack_patterns[23].pattern, "nikto", 255); global_firewall.attack_patterns[23].severity = 10;
    strncpy(global_firewall.attack_patterns[24].pattern, "masscan", 255); global_firewall.attack_patterns[24].severity = 10;

    // Set counts
    global_firewall.attack_pattern_count = 25; 

    // Mark as initialized
    global_firewall.is_initialized = 1;
    
    pthread_mutex_unlock(&global_firewall.mutex);
    log_message("FIREWALL", "Enterprise Firewall initialized. Signature Database Loaded.");
    return 0;
}

int firewall_check_request(const char *ip_address, const char *api_key) {
    return firewall_check_request_enhanced(ip_address, api_key, NULL, NULL);
}

int firewall_check_request_enhanced(const char *ip_address, const char *api_key, const char *request_data, const char *user_agent) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    global_firewall.stats.total_requests++;
    
    // Check whitelist first
    if (is_ip_whitelisted(ip_address)) {
        pthread_mutex_unlock(&global_firewall.mutex);
        return 0;
    }
    
    // Check blacklist (Hard Block)
    if (is_ip_blacklisted(ip_address)) {
        global_firewall.stats.blocked_requests++;
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    
    FirewallEntry *entry = find_entry(ip_address);
    if (!entry) {
        entry = add_entry(ip_address);
        if (!entry) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
    }
    
    // Check if currently blocked
    if (entry->is_blocked) {
        time_t current_time = time(NULL);
        if (current_time - entry->block_start_time >= global_firewall.rate_limit_config.block_duration_minutes * 60) {
            entry->is_blocked = 0;
            entry->request_count = 0;
            entry->suspicious_score = 0;
        } else {
            global_firewall.stats.blocked_requests++;
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;  
        }
    }
    
    // === REAL WAF LOGIC ===
    
    // 1. User-Agent Scanning Detection
    if (user_agent) {
        int scanner_severity = detect_attack_pattern(user_agent);
        if (scanner_severity >= 10) {
            log_message("FIREWALL_WAF", "Malicious Scanner Blocked via User-Agent");
            entry->is_blocked = 1;
            entry->block_start_time = time(NULL);
            update_suspicion(entry, 50); // Heavy penalty
            global_firewall.stats.blocked_requests++;
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
    }

    // 2. Request Content Analysis (SQLi/XSS)
    if (request_data) {
        int payload_severity = detect_attack_pattern(request_data);
        if (payload_severity > 5) {
            update_suspicion(entry, payload_severity * 5);
            
            char log_msg[512];
            snprintf(log_msg, sizeof(log_msg), "Suspicious Payload Detected (Severity %d) from %s", payload_severity, ip_address);
            log_message("FIREWALL_WAF", log_msg);

            if (entry->suspicious_score >= global_firewall.rate_limit_config.suspicious_threshold) {
                entry->is_blocked = 1;
                entry->block_start_time = time(NULL);
                global_firewall.stats.blocked_requests++;
                pthread_mutex_unlock(&global_firewall.mutex);
                return -1;
            }
        }
    }
    
    // Validate API key if required
    if (global_firewall.api_key_count > 0) {
        if (!api_key) {
            global_firewall.stats.invalid_api_keys++;
            update_suspicion(entry, 5);
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
            global_firewall.stats.invalid_api_keys++;
            update_suspicion(entry, 10);
            
            if (entry->suspicious_score >= global_firewall.rate_limit_config.suspicious_threshold) {
                entry->is_blocked = 1;
                entry->block_start_time = time(NULL);
                global_firewall.stats.blocked_requests++;
                pthread_mutex_unlock(&global_firewall.mutex);
                return -1;
            }
            
            pthread_mutex_unlock(&global_firewall.mutex);
            return -1;
        }
    }
    
    // Update request count with sliding window
    entry->request_count++;
    entry->last_request = time(NULL);
    
    // Decay suspicion slightly for good behavior per request
    update_suspicion(entry, 0); 
    
    int requests_in_window = get_requests_in_window(ip_address, 60);
    
    if (requests_in_window > global_firewall.rate_limit_config.max_requests_per_minute) {
        entry->is_blocked = 1;
        entry->block_start_time = time(NULL);
        global_firewall.stats.blocked_requests++;
        
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
    entry->block_start_time = time(NULL);
    
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
        entry->suspicious_score = 0;
        
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

// ===== Enhanced Functions =====
int firewall_add_to_whitelist(const char *ip_address, int permanent, time_t duration) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    // Check if already whitelisted
    for (int i = 0; i < global_firewall.whitelist_count; i++) {
        if (strcmp(global_firewall.whitelist[i].ip_address, ip_address) == 0) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return 0;
        }
    }
    
    // Add new whitelist entry
    WhitelistEntry *new_whitelist = realloc(global_firewall.whitelist, 
                                           sizeof(WhitelistEntry) * (global_firewall.whitelist_count + 1));
    if (!new_whitelist) {
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    
    global_firewall.whitelist = new_whitelist;
    strncpy(global_firewall.whitelist[global_firewall.whitelist_count].ip_address, ip_address, INET6_ADDRSTRLEN - 1);
    global_firewall.whitelist[global_firewall.whitelist_count].ip_address[INET6_ADDRSTRLEN - 1] = '\0';
    global_firewall.whitelist[global_firewall.whitelist_count].permanent = permanent;
    global_firewall.whitelist[global_firewall.whitelist_count].expiry_time = permanent ? 0 : time(NULL) + duration;
    global_firewall.whitelist_count++;
    global_firewall.stats.whitelisted_ips = global_firewall.whitelist_count;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "IP added to whitelist: %s (%s)", ip_address, permanent ? "permanent" : "temporary");
    log_message("FIREWALL", log_msg);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_remove_from_whitelist(const char *ip_address) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    int found = 0;
    for (int i = 0; i < global_firewall.whitelist_count; i++) {
        if (strcmp(global_firewall.whitelist[i].ip_address, ip_address) == 0) {
            // Move last element to current position
            if (i < global_firewall.whitelist_count - 1) {
                global_firewall.whitelist[i] = global_firewall.whitelist[global_firewall.whitelist_count - 1];
            }
            global_firewall.whitelist_count--;
            found = 1;
            break;
        }
    }
    
    if (found) {
        global_firewall.stats.whitelisted_ips = global_firewall.whitelist_count;
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "IP removed from whitelist: %s", ip_address);
        log_message("FIREWALL", log_msg);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return found ? 0 : -1;
}

int firewall_add_to_blacklist(const char *ip_address, BlockReason reason, const char *description) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    // Check if already blacklisted
    for (int i = 0; i < global_firewall.blacklist_count; i++) {
        if (strcmp(global_firewall.blacklist[i].ip_address, ip_address) == 0) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return 0;
        }
    }
    
    // Add new blacklist entry
    BlacklistEntry *new_blacklist = realloc(global_firewall.blacklist, 
                                           sizeof(BlacklistEntry) * (global_firewall.blacklist_count + 1));
    if (!new_blacklist) {
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    
    global_firewall.blacklist = new_blacklist;
    strncpy(global_firewall.blacklist[global_firewall.blacklist_count].ip_address, ip_address, INET6_ADDRSTRLEN - 1);
    global_firewall.blacklist[global_firewall.blacklist_count].ip_address[INET6_ADDRSTRLEN - 1] = '\0';
    global_firewall.blacklist[global_firewall.blacklist_count].added_time = time(NULL);
    global_firewall.blacklist[global_firewall.blacklist_count].reason = reason;
    strncpy(global_firewall.blacklist[global_firewall.blacklist_count].description, description, 255);
    global_firewall.blacklist[global_firewall.blacklist_count].description[255] = '\0';
    global_firewall.blacklist_count++;
    global_firewall.stats.blacklisted_ips = global_firewall.blacklist_count;
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "IP added to blacklist: %s - %s", ip_address, description);
    log_message("FIREWALL", log_msg);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_remove_from_blacklist(const char *ip_address) {
    if (!ip_address) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    int found = 0;
    for (int i = 0; i < global_firewall.blacklist_count; i++) {
        if (strcmp(global_firewall.blacklist[i].ip_address, ip_address) == 0) {
            // Move last element to current position
            if (i < global_firewall.blacklist_count - 1) {
                global_firewall.blacklist[i] = global_firewall.blacklist[global_firewall.blacklist_count - 1];
            }
            global_firewall.blacklist_count--;
            found = 1;
            break;
        }
    }
    
    if (found) {
        global_firewall.stats.blacklisted_ips = global_firewall.blacklist_count;
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "IP removed from blacklist: %s", ip_address);
        log_message("FIREWALL", log_msg);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return found ? 0 : -1;
}

int firewall_add_attack_pattern(const char *pattern, int severity) {
    if (!pattern || severity < 1 || severity > 10) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    // Check if pattern already exists
    for (int i = 0; i < global_firewall.attack_pattern_count; i++) {
        if (strcmp(global_firewall.attack_patterns[i].pattern, pattern) == 0) {
            pthread_mutex_unlock(&global_firewall.mutex);
            return 0;
        }
    }
    
    // Add new attack pattern
    AttackPattern *new_patterns = realloc(global_firewall.attack_patterns, 
                                        sizeof(AttackPattern) * (global_firewall.attack_pattern_count + 1));
    if (!new_patterns) {
        pthread_mutex_unlock(&global_firewall.mutex);
        return -1;
    }
    
    global_firewall.attack_patterns = new_patterns;
    strncpy(global_firewall.attack_patterns[global_firewall.attack_pattern_count].pattern, pattern, 255);
    global_firewall.attack_patterns[global_firewall.attack_pattern_count].pattern[255] = '\0';
    global_firewall.attack_patterns[global_firewall.attack_pattern_count].severity = severity;
    global_firewall.attack_patterns[global_firewall.attack_pattern_count].last_detected = 0;
    global_firewall.attack_pattern_count++;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Attack pattern added: %s (severity: %d)", pattern, severity);
    log_message("FIREWALL", log_msg);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_remove_attack_pattern(const char *pattern) {
    if (!pattern) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    int found = 0;
    for (int i = 0; i < global_firewall.attack_pattern_count; i++) {
        if (strcmp(global_firewall.attack_patterns[i].pattern, pattern) == 0) {
            // Move last element to current position
            if (i < global_firewall.attack_pattern_count - 1) {
                global_firewall.attack_patterns[i] = global_firewall.attack_patterns[global_firewall.attack_pattern_count - 1];
            }
            global_firewall.attack_pattern_count--;
            found = 1;
            break;
        }
    }
    
    if (found) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Attack pattern removed: %s", pattern);
        log_message("FIREWALL", log_msg);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return found ? 0 : -1;
}

int firewall_get_stats(FirewallStats *stats) {
    if (!stats) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    memcpy(stats, &global_firewall.stats, sizeof(FirewallStats));
    pthread_mutex_unlock(&global_firewall.mutex);
    
    return 0;
}

int firewall_get_suspicious_ips(char ***suspicious_ips, int *count, int threshold) {
    if (!suspicious_ips || !count || threshold < 0) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    int suspicious_count = 0;
    for (int i = 0; i < global_firewall.entry_count; i++) {
        if (global_firewall.entries[i].suspicious_score > threshold) {
            suspicious_count++;
        }
    }
    
    *count = suspicious_count;
    *suspicious_ips = malloc(sizeof(char *) * suspicious_count);
    
    int index = 0;
    for (int i = 0; i < global_firewall.entry_count; i++) {
        if (global_firewall.entries[i].suspicious_score > threshold) {
            (*suspicious_ips)[index] = strdup(global_firewall.entries[i].ip_address);
            index++;
        }
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_get_whitelisted_ips(char ***whitelisted_ips, int *count) {
    if (!whitelisted_ips || !count) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    *count = global_firewall.whitelist_count;
    *whitelisted_ips = malloc(sizeof(char *) * global_firewall.whitelist_count);
    
    for (int i = 0; i < global_firewall.whitelist_count; i++) {
        (*whitelisted_ips)[i] = strdup(global_firewall.whitelist[i].ip_address);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_get_blacklisted_ips(char ***blacklisted_ips, int *count) {
    if (!blacklisted_ips || !count) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    *count = global_firewall.blacklist_count;
    *blacklisted_ips = malloc(sizeof(char *) * global_firewall.blacklist_count);
    
    for (int i = 0; i < global_firewall.blacklist_count; i++) {
        (*blacklisted_ips)[i] = strdup(global_firewall.blacklist[i].ip_address);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_get_attack_patterns(AttackPattern **patterns, int *count) {
    if (!patterns || !count) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    *count = global_firewall.attack_pattern_count;
    *patterns = malloc(sizeof(AttackPattern) * global_firewall.attack_pattern_count);
    
    memcpy(*patterns, global_firewall.attack_patterns, sizeof(AttackPattern) * global_firewall.attack_pattern_count);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_configure_rate_limits(const RateLimitConfig *config) {
    if (!config) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    memcpy(&global_firewall.rate_limit_config, config, sizeof(RateLimitConfig));
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "Rate limits configured: %d req/min, %d min block, %d suspicious threshold", 
             config->max_requests_per_minute, config->block_duration_minutes, config->suspicious_threshold);
    log_message("FIREWALL", log_msg);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return 0;
}

int firewall_get_rate_limit_config(RateLimitConfig *config) {
    if (!config) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    memcpy(config, &global_firewall.rate_limit_config, sizeof(RateLimitConfig));
    pthread_mutex_unlock(&global_firewall.mutex);
    
    return 0;
}

int firewall_is_whitelisted(const char *ip_address) {
    if (!ip_address) {
        return 0;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    int result = is_ip_whitelisted(ip_address);
    pthread_mutex_unlock(&global_firewall.mutex);
    
    return result;
}

int firewall_is_blacklisted(const char *ip_address) {
    if (!ip_address) {
        return 0;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    int result = is_ip_blacklisted(ip_address);
    pthread_mutex_unlock(&global_firewall.mutex);
    
    return result;
}

int firewall_get_block_reason(const char *ip_address, BlockReason *reason) {
    if (!ip_address || !reason) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    FirewallEntry *entry = find_entry(ip_address);
    if (entry && entry->is_blocked) {
        // Determine block reason based on entry properties
        if (entry->suspicious_score >= global_firewall.rate_limit_config.suspicious_threshold) {
            *reason = BLOCK_REASON_SUSPICIOUS;
        } else if (detect_brute_force(ip_address)) {
            *reason = BLOCK_REASON_BRUTE_FORCE;
        } else {
            *reason = BLOCK_REASON_RATE_LIMIT;
        }
        
        pthread_mutex_unlock(&global_firewall.mutex);
        return 0;
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    return -1;
}

int firewall_clear_all(void) {
    pthread_mutex_lock(&global_firewall.mutex);
    
    // Clear entries
    for (int i = 0; i < global_firewall.entry_count; i++) {
        free(global_firewall.entries[i].ip_address);
    }
    free(global_firewall.entries);
    
    // Clear API keys
    for (int i = 0; i < global_firewall.api_key_count; i++) {
        free(global_firewall.allowed_api_keys[i]);
    }
    free(global_firewall.allowed_api_keys);
    
    // Clear enhanced features
    free(global_firewall.attack_patterns);
    free(global_firewall.whitelist);
    free(global_firewall.blacklist);
    
    // Reset counters
    global_firewall.entry_count = 0;
    global_firewall.entry_capacity = 1024;
    global_firewall.api_key_count = 0;
    global_firewall.attack_pattern_count = 0;
    global_firewall.whitelist_count = 0;
    global_firewall.blacklist_count = 0;
    
    // Reset statistics
    memset(&global_firewall.stats, 0, sizeof(FirewallStats));
    global_firewall.stats.start_time = time(NULL);
    
    // Reset Init State (Re-fire init next time)
    global_firewall.is_initialized = 0;
    
    pthread_mutex_unlock(&global_firewall.mutex);
    
    log_message("FIREWALL", "All firewall data cleared");
    return 0;
}

int firewall_save_config(const char *filename) {
    if (!filename) {
        return -1;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    // Save basic configuration
    fprintf(file, "# AIONIC Firewall Configuration\n");
    fprintf(file, "# Generated at: %s", ctime(&global_firewall.stats.start_time));
    fprintf(file, "\n[rate_limits]\n");
    fprintf(file, "max_requests_per_minute = %d\n", global_firewall.rate_limit_config.max_requests_per_minute);
    fprintf(file, "block_duration_minutes = %d\n", global_firewall.rate_limit_config.block_duration_minutes);
    fprintf(file, "suspicious_threshold = %d\n", global_firewall.rate_limit_config.suspicious_threshold);
    fprintf(file, "brute_force_threshold = %d\n", global_firewall.rate_limit_config.brute_force_threshold);
    fprintf(file, "brute_force_window_seconds = %d\n", global_firewall.rate_limit_config.brute_force_window_seconds);
    
    // Save whitelist
    fprintf(file, "\n[whitelist]\n");
    for (int i = 0; i < global_firewall.whitelist_count; i++) {
        fprintf(file, "%s,%d,%ld\n", global_firewall.whitelist[i].ip_address, 
                global_firewall.whitelist[i].permanent, global_firewall.whitelist[i].expiry_time);
    }
    
    // Save blacklist
    fprintf(file, "\n[blacklist]\n");
    for (int i = 0; i < global_firewall.blacklist_count; i++) {
        fprintf(file, "%s,%d,%s\n", global_firewall.blacklist[i].ip_address, 
                global_firewall.blacklist[i].reason, global_firewall.blacklist[i].description);
    }
    
    // Save attack patterns
    fprintf(file, "\n[attack_patterns]\n");
    for (int i = 0; i < global_firewall.attack_pattern_count; i++) {
        fprintf(file, "%s,%d\n", global_firewall.attack_patterns[i].pattern, 
                global_firewall.attack_patterns[i].severity);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    fclose(file);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Firewall configuration saved to: %s", filename);
    log_message("FIREWALL", log_msg);
    
    return 0;
}

int firewall_load_config(const char *filename) {
    if (!filename) {
        return -1;
    }
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    char line[1024];
    int section = 0; // 0: none, 1: rate_limits, 2: whitelist, 3: blacklist, 4: attack_patterns
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = '\0';
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        
        // Check for sections
        if (strcmp(line, "[rate_limits]") == 0) {
            section = 1;
            continue;
        } else if (strcmp(line, "[whitelist]") == 0) {
            section = 2;
            continue;
        } else if (strcmp(line, "[blacklist]") == 0) {
            section = 3;
            continue;
        } else if (strcmp(line, "[attack_patterns]") == 0) {
            section = 4;
            continue;
        }
        
        // Parse section content
        switch (section) {
            case 1: { // rate_limits
                char key[256];
                int value;
                if (sscanf(line, "%[^=]=%d", key, &value) == 2) {
                    // Trim whitespace from key
                    char *end = key + strlen(key) - 1;
                    while (end > key && isspace(*end)) end--;
                    *(end + 1) = '\0';
                    
                    if (strcmp(key, "max_requests_per_minute") == 0) {
                        global_firewall.rate_limit_config.max_requests_per_minute = value;
                    } else if (strcmp(key, "block_duration_minutes") == 0) {
                        global_firewall.rate_limit_config.block_duration_minutes = value;
                    } else if (strcmp(key, "suspicious_threshold") == 0) {
                        global_firewall.rate_limit_config.suspicious_threshold = value;
                    } else if (strcmp(key, "brute_force_threshold") == 0) {
                        global_firewall.rate_limit_config.brute_force_threshold = value;
                    } else if (strcmp(key, "brute_force_window_seconds") == 0) {
                        global_firewall.rate_limit_config.brute_force_window_seconds = value;
                    }
                }
                break;
            }
            case 2: { // whitelist
                char ip[INET6_ADDRSTRLEN];
                int permanent;
                time_t expiry;
                if (sscanf(line, "%[^,],%d,%ld", ip, &permanent, &expiry) == 3) {
                    firewall_add_to_whitelist(ip, permanent, expiry - time(NULL));
                }
                break;
            }
            case 3: { // blacklist
                char ip[INET6_ADDRSTRLEN];
                int reason;
                char description[256];
                if (sscanf(line, "%[^,],%d,%[^\n]", ip, &reason, description) == 3) {
                    firewall_add_to_blacklist(ip, reason, description);
                }
                break;
            }
            case 4: { // attack_patterns
                char pattern[256];
                int severity;
                if (sscanf(line, "%[^,],%d", pattern, &severity) == 2) {
                    firewall_add_attack_pattern(pattern, severity);
                }
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    fclose(file);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Firewall configuration loaded from: %s", filename);
    log_message("FIREWALL", log_msg);
    
    return 0;
}

int firewall_export_stats(const char *filename, int format) {
    if (!filename) {
        return -1;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        return -1;
    }
    
    pthread_mutex_lock(&global_firewall.mutex);
    
    if (format == 0) { // JSON format
        fprintf(file, "{\n");
        fprintf(file, "  \"version\": \"%s\",\n", FIREWALL_VERSION_STRING);
        fprintf(file, "  \"timestamp\": %ld,\n", time(NULL));
        fprintf(file, "  \"uptime\": %ld,\n", time(NULL) - global_firewall.stats.start_time);
        fprintf(file, "  \"statistics\": {\n");
        fprintf(file, "    \"total_requests\": %lu,\n", global_firewall.stats.total_requests);
        fprintf(file, "    \"blocked_requests\": %lu,\n", global_firewall.stats.blocked_requests);
        fprintf(file, "    \"suspicious_activities\": %lu,\n", global_firewall.stats.suspicious_activities);
        fprintf(file, "    \"brute_force_attempts\": %lu,\n", global_firewall.stats.brute_force_attempts);
        fprintf(file, "    \"invalid_api_keys\": %lu,\n", global_firewall.stats.invalid_api_keys);
        fprintf(file, "    \"attack_pattern_hits\": %lu\n", global_firewall.stats.attack_pattern_hits);
        fprintf(file, "  },\n");
        fprintf(file, "  \"counts\": {\n");
        fprintf(file, "    \"active_entries\": %d,\n", global_firewall.stats.active_entries);
        fprintf(file, "    \"whitelisted_ips\": %d,\n", global_firewall.stats.whitelisted_ips);
        fprintf(file, "    \"blacklisted_ips\": %d\n", global_firewall.stats.blacklisted_ips);
        fprintf(file, "  }\n");
        fprintf(file, "}\n");
    } else { // CSV format
        fprintf(file, "Metric,Value\n");
        fprintf(file, "Version,%s\n", FIREWALL_VERSION_STRING);
        fprintf(file, "Timestamp,%ld\n", time(NULL));
        fprintf(file, "Uptime,%ld\n", time(NULL) - global_firewall.stats.start_time);
        fprintf(file, "Total Requests,%lu\n", global_firewall.stats.total_requests);
        fprintf(file, "Blocked Requests,%lu\n", global_firewall.stats.blocked_requests);
        fprintf(file, "Suspicious Activities,%lu\n", global_firewall.stats.suspicious_activities);
        fprintf(file, "Brute Force Attempts,%lu\n", global_firewall.stats.brute_force_attempts);
        fprintf(file, "Invalid API Keys,%lu\n", global_firewall.stats.invalid_api_keys);
        fprintf(file, "Attack Pattern Hits,%lu\n", global_firewall.stats.attack_pattern_hits);
        fprintf(file, "Active Entries,%d\n", global_firewall.stats.active_entries);
        fprintf(file, "Whitelisted IPs,%d\n", global_firewall.stats.whitelisted_ips);
        fprintf(file, "Blacklisted IPs,%d\n", global_firewall.stats.blacklisted_ips);
    }
    
    pthread_mutex_unlock(&global_firewall.mutex);
    fclose(file);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Firewall statistics exported to: %s", filename);
    log_message("FIREWALL", log_msg);
    
    return 0;
}

int firewall_import_attack_patterns(const char *filename) {
    if (!filename) {
        return -1;
    }
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        return -1;
    }
    
    char line[1024];
    int imported = 0;
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = '\0';
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        
        // Parse pattern and severity
        char pattern[256];
        int severity;
        if (sscanf(line, "%[^,],%d", pattern, &severity) == 2) {
            if (firewall_add_attack_pattern(pattern, severity) == 0) {
                imported++;
            }
        }
    }
    
    fclose(file);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Imported %d attack patterns from: %s", imported, filename);
    log_message("FIREWALL", log_msg);
    
    return imported;
}

void firewall_cleanup() {
    pthread_mutex_lock(&global_firewall.mutex);
    
    // Clean up entries
    for (int i = 0; i < global_firewall.entry_count; i++) {
        free(global_firewall.entries[i].ip_address);
    }
    free(global_firewall.entries);
    
    // Clean up API keys
    for (int i = 0; i < global_firewall.api_key_count; i++) {
        free(global_firewall.allowed_api_keys[i]);
    }
    free(global_firewall.allowed_api_keys);
    
    // Clean up enhanced features
    free(global_firewall.attack_patterns);
    free(global_firewall.whitelist);
    free(global_firewall.blacklist);
    
    pthread_mutex_unlock(&global_firewall.mutex);
    pthread_mutex_destroy(&global_firewall.mutex);
    
    log_message("FIREWALL", "Enhanced firewall cleaned up");
}
