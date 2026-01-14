#ifndef AIONIC_FIREWALL_H
#define AIONIC_FIREWALL_H

#include <time.h>
#include <sys/types.h> // For in_addr_t if needed

// ===== Constants =====
#define DEFAULT_RATE_LIMIT_PER_MINUTE 60
#define DEFAULT_BLOCK_DURATION_MINUTES 5
#define DEFAULT_SUSPICIOUS_THRESHOLD 50
#define DEFAULT_BRUTE_FORCE_THRESHOLD 10
#define FIREWALL_VERSION_STRING "2.0.0"

// ===== Enumerations =====
typedef enum {
    BLOCK_REASON_RATE_LIMIT = 0,
    BLOCK_REASON_BRUTE_FORCE = 1,
    BLOCK_REASON_ATTACK_PATTERN = 2,
    BLOCK_REASON_SUSPICIOUS = 3
} BlockReason;

// ===== Structures =====

typedef struct {
    char *ip_address;
    int request_count;
    time_t last_request;
    int is_blocked;
    time_t block_start_time;
    int suspicious_score;
} FirewallEntry;

typedef struct {
    char *ip_address;
    int permanent;
    time_t expiry_time;
} WhitelistEntry;

typedef struct {
    char *ip_address;
    BlockReason reason;
    char description[256];
    time_t added_time;
} BlacklistEntry;

typedef struct {
    char pattern[256];
    int severity;
    time_t last_detected;
} AttackPattern;

typedef struct {
    int max_requests_per_minute;
    int block_duration_minutes;
    int suspicious_threshold;
    int brute_force_threshold;
    int brute_force_window_seconds;
} RateLimitConfig;

typedef struct {
    unsigned long total_requests;
    unsigned long blocked_requests;
    unsigned long suspicious_activities;
    unsigned long brute_force_attempts;
    unsigned long invalid_api_keys;
    unsigned long attack_pattern_hits;
    int active_entries;
    int whitelisted_ips;
    int blacklisted_ips;
    time_t start_time;
} FirewallStats;

// Forward declaration for Config (assuming Config is defined in utils.h or server.h)
struct Config;

// ===== Public API =====

/**
 * Initialize the firewall system.
 */
int firewall_init(const struct Config *config);

/**
 * Check a standard request (legacy support).
 * Calls the enhanced version internally.
 */
int firewall_check_request(const char *ip_address, const char *api_key);

/**
 * Check a request with enhanced details (WAF level).
 * 
 * @param ip_address The IP address of the client.
 * @param api_key The provided API key (if any).
 * @param request_data The raw request body/payload (for SQLi/XSS detection).
 * @param user_agent The HTTP User-Agent string (for scanner detection).
 * @return 0 if allowed, -1 if blocked.
 */
int firewall_check_request_enhanced(const char *ip_address, const char *api_key, const char *request_data, const char *user_agent);

/**
 * Manually block an IP address.
 */
int firewall_block_ip(const char *ip_address);

/**
 * Manually unblock an IP address.
 */
int firewall_unblock_ip(const char *ip_address);

/**
 * Get list of currently blocked IPs.
 * Note: The caller is responsible for freeing the returned array and strings.
 */
int firewall_get_blocked_ips(char ***blocked_ips, int *count);

/**
 * Add IP to whitelist.
 */
int firewall_add_to_whitelist(const char *ip_address, int permanent, time_t duration);

/**
 * Remove IP from whitelist.
 */
int firewall_remove_from_whitelist(const char *ip_address);

/**
 * Add IP to blacklist.
 */
int firewall_add_to_blacklist(const char *ip_address, BlockReason reason, const char *description);

/**
 * Remove IP from blacklist.
 */
int firewall_remove_from_blacklist(const char *ip_address);

/**
 * Add a new attack pattern signature.
 */
int firewall_add_attack_pattern(const char *pattern, int severity);

/**
 * Remove an attack pattern signature.
 */
int firewall_remove_attack_pattern(const char *pattern);

/**
 * Retrieve current firewall statistics.
 */
int firewall_get_stats(FirewallStats *stats);

/**
 * Get list of suspicious IPs (based on score threshold).
 */
int firewall_get_suspicious_ips(char ***suspicious_ips, int *count, int threshold);

/**
 * Get list of whitelisted IPs.
 */
int firewall_get_whitelisted_ips(char ***whitelisted_ips, int *count);

/**
 * Get list of blacklisted IPs.
 */
int firewall_get_blacklisted_ips(char ***blacklisted_ips, int *count);

/**
 * Get list of attack patterns.
 */
int firewall_get_attack_patterns(AttackPattern **patterns, int *count);

/**
 * Configure rate limits dynamically.
 */
int firewall_configure_rate_limits(const RateLimitConfig *config);

/**
 * Get current rate limit configuration.
 */
int firewall_get_rate_limit_config(RateLimitConfig *config);

/**
 * Check if IP is whitelisted.
 */
int firewall_is_whitelisted(const char *ip_address);

/**
 * Check if IP is blacklisted.
 */
int firewall_is_blacklisted(const char *ip_address);

/**
 * Get the reason why an IP was blocked.
 */
int firewall_get_block_reason(const char *ip_address, BlockReason *reason);

/**
 * Clear all firewall data (IPs, patterns, keys).
 */
int firewall_clear_all(void);

/**
 * Save firewall configuration to a file.
 */
int firewall_save_config(const char *filename);

/**
 * Load firewall configuration from a file.
 */
int firewall_load_config(const char *filename);

/**
 * Export firewall statistics to a file.
 * @param format 0 for JSON, 1 for CSV.
 */
int firewall_export_stats(const char *filename, int format);

/**
 * Import attack patterns from a CSV file.
 */
int firewall_import_attack_patterns(const char *filename);

/**
 * Cleanup and release all firewall resources.
 */
void firewall_cleanup(void);

#endif // AIONIC_FIREWALL_H
