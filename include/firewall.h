#ifndef AIONIC_FIREWALL_H
#define AIONIC_FIREWALL_H

#include "config.h"
#include <netinet/in.h>  // For INET6_ADDRSTRLEN
#include <stdint.h>     // For uint64_t

// ===== Enhanced Firewall Entry =====
typedef struct {
    char *ip_address;
    int request_count;
    time_t last_request;
    int is_blocked;
    time_t block_start_time;
    int suspicious_score;      // Score for suspicious behavior
} FirewallEntry;

// ===== Block Reasons =====
typedef enum {
    BLOCK_REASON_RATE_LIMIT,     // Blocked due to rate limit
    BLOCK_REASON_MANUAL,         // Manually blocked
    BLOCK_REASON_SUSPICIOUS,     // Suspicious activity
    BLOCK_REASON_BRUTE_FORCE,    // Brute force attempt
    BLOCK_REASON_INVALID_API_KEY, // Invalid API key
    BLOCK_REASON_ATTACK_PATTERN  // Attack pattern detected
} BlockReason;

// ===== Attack Pattern =====
typedef struct {
    char pattern[256];           // Attack pattern (e.g., SQL injection)
    int severity;                // Severity level (1-10)
    time_t last_detected;       // Last detection time
} AttackPattern;

// ===== Whitelist Entry =====
typedef struct {
    char ip_address[INET6_ADDRSTRLEN];  // IP address
    time_t expiry_time;                 // Expiry time (0 for permanent)
    int permanent;                      // Permanent whitelist flag
} WhitelistEntry;

// ===== Blacklist Entry =====
typedef struct {
    char ip_address[INET6_ADDRSTRLEN];  // IP address
    time_t added_time;                  // When it was added
    BlockReason reason;                 // Reason for blacklisting
    char description[256];              // Description of the reason
} BlacklistEntry;

// ===== Firewall Statistics =====
typedef struct {
    uint64_t total_requests;            // Total requests processed
    uint64_t blocked_requests;          // Total blocked requests
    uint64_t suspicious_activities;     // Suspicious activities detected
    uint64_t brute_force_attempts;      // Brute force attempts
    uint64_t invalid_api_keys;          // Invalid API key attempts
    uint64_t attack_pattern_hits;       // Attack pattern matches
    time_t start_time;                  // Firewall start time
    int active_entries;                 // Currently tracked IPs
    int whitelisted_ips;                // Number of whitelisted IPs
    int blacklisted_ips;                // Number of blacklisted IPs
} FirewallStats;

// ===== Rate Limit Configuration =====
typedef struct {
    int max_requests_per_minute;        // Max requests per minute per IP
    int block_duration_minutes;         // How long to block (minutes)
    int suspicious_threshold;          // Threshold for suspicious behavior
    int brute_force_threshold;         // Threshold for brute force detection
    int brute_force_window_seconds;     // Time window for brute force detection
} RateLimitConfig;

// ===== Core Functions (Original Interface Maintained) =====
/**
 * @brief Initialize the firewall system
 * @param config Configuration settings
 * @return 0 on success, -1 on failure
 */
int firewall_init(const Config *config);

/**
 * @brief Check if a request should be allowed
 * @param ip_address Client IP address
 * @param api_key API key (can be NULL)
 * @return 0 if allowed, -1 if blocked
 */
int firewall_check_request(const char *ip_address, const char *api_key);

/**
 * @brief Manually block an IP address
 * @param ip_address IP address to block
 * @return 0 on success, -1 on failure
 */
int firewall_block_ip(const char *ip_address);

/**
 * @brief Unblock a previously blocked IP address
 * @param ip_address IP address to unblock
 * @return 0 on success, -1 on failure
 */
int firewall_unblock_ip(const char *ip_address);

/**
 * @brief Get list of currently blocked IP addresses
 * @param blocked_ips Output array of blocked IPs
 * @param count Number of blocked IPs
 * @return 0 on success, -1 on failure
 */
int firewall_get_blocked_ips(char ***blocked_ips, int *count);

/**
 * @brief Clean up firewall resources
 */
void firewall_cleanup();

// ===== Enhanced Functions (New Features) =====

/**
 * @brief Enhanced request checking with attack detection
 * @param ip_address Client IP address
 * @param api_key API key (can be NULL)
 * @param request_data HTTP request data for pattern matching
 * @return 0 if allowed, -1 if blocked
 */
int firewall_check_request_enhanced(const char *ip_address, const char *api_key, const char *request_data);

/**
 * @brief Add IP address to whitelist
 * @param ip_address IP address to whitelist
 * @param permanent Whether the whitelist entry is permanent
 * @param duration Duration in seconds (ignored if permanent)
 * @return 0 on success, -1 on failure
 */
int firewall_add_to_whitelist(const char *ip_address, int permanent, time_t duration);

/**
 * @brief Remove IP address from whitelist
 * @param ip_address IP address to remove from whitelist
 * @return 0 on success, -1 on failure
 */
int firewall_remove_from_whitelist(const char *ip_address);

/**
 * @brief Add IP address to blacklist
 * @param ip_address IP address to blacklist
 * @param reason Reason for blacklisting
 * @param description Description of the reason
 * @return 0 on success, -1 on failure
 */
int firewall_add_to_blacklist(const char *ip_address, BlockReason reason, const char *description);

/**
 * @brief Remove IP address from blacklist
 * @param ip_address IP address to remove from blacklist
 * @return 0 on success, -1 on failure
 */
int firewall_remove_from_blacklist(const char *ip_address);

/**
 * @brief Add attack pattern for detection
 * @param pattern Pattern to detect (e.g., " UNION ")
 * @param severity Severity level (1-10)
 * @return 0 on success, -1 on failure
 */
int firewall_add_attack_pattern(const char *pattern, int severity);

/**
 * @brief Remove attack pattern
 * @param pattern Pattern to remove
 * @return 0 on success, -1 on failure
 */
int firewall_remove_attack_pattern(const char *pattern);

/**
 * @brief Get current firewall statistics
 * @param stats Output structure for statistics
 * @return 0 on success, -1 on failure
 */
int firewall_get_stats(FirewallStats *stats);

/**
 * @brief Get list of suspicious IP addresses
 * @param suspicious_ips Output array of suspicious IPs
 * @param count Number of suspicious IPs
 * @param threshold Score threshold for suspicious behavior
 * @return 0 on success, -1 on failure
 */
int firewall_get_suspicious_ips(char ***suspicious_ips, int *count, int threshold);

/**
 * @brief Get list of whitelisted IP addresses
 * @param whitelisted_ips Output array of whitelisted IPs
 * @param count Number of whitelisted IPs
 * @return 0 on success, -1 on failure
 */
int firewall_get_whitelisted_ips(char ***whitelisted_ips, int *count);

/**
 * @brief Get list of blacklisted IP addresses
 * @param blacklisted_ips Output array of blacklisted IPs
 * @param count Number of blacklisted IPs
 * @return 0 on success, -1 on failure
 */
int firewall_get_blacklisted_ips(char ***blacklisted_ips, int *count);

/**
 * @brief Get list of configured attack patterns
 * @param patterns Output array of attack patterns
 * @param count Number of attack patterns
 * @return 0 on success, -1 on failure
 */
int firewall_get_attack_patterns(AttackPattern **patterns, int *count);

/**
 * @brief Configure rate limiting parameters
 * @param config Rate limit configuration
 * @return 0 on success, -1 on failure
 */
int firewall_configure_rate_limits(const RateLimitConfig *config);

/**
 * @brief Get current rate limit configuration
 * @param config Output structure for rate limit configuration
 * @return 0 on success, -1 on failure
 */
int firewall_get_rate_limit_config(RateLimitConfig *config);

/**
 * @brief Check if an IP is whitelisted
 * @param ip_address IP address to check
 * @return 1 if whitelisted, 0 if not
 */
int firewall_is_whitelisted(const char *ip_address);

/**
 * @brief Check if an IP is blacklisted
 * @param ip_address IP address to check
 * @return 1 if blacklisted, 0 if not
 */
int firewall_is_blacklisted(const char *ip_address);

/**
 * @brief Get block reason for an IP
 * @param ip_address IP address to check
 * @param reason Output block reason
 * @return 0 on success, -1 if not blocked or error
 */
int firewall_get_block_reason(const char *ip_address, BlockReason *reason);

/**
 * @brief Clear all firewall data (for testing/reset)
 * @return 0 on success, -1 on failure
 */
int firewall_clear_all(void);

/**
 * @brief Save firewall configuration to file
 * @param filename Path to save configuration
 * @return 0 on success, -1 on failure
 */
int firewall_save_config(const char *filename);

/**
 * @brief Load firewall configuration from file
 * @param filename Path to load configuration from
 * @return 0 on success, -1 on failure
 */
int firewall_load_config(const char *filename);

/**
 * @brief Export firewall statistics to file
 * @param filename Path to export statistics
 * @param format Export format (0=JSON, 1=CSV)
 * @return 0 on success, -1 on failure
 */
int firewall_export_stats(const char *filename, int format);

/**
 * @brief Import attack patterns from file
 * @param filename Path to import patterns from
 * @return 0 on success, -1 on failure
 */
int firewall_import_attack_patterns(const char *filename);

// ===== Utility Macros =====

/**
 * @brief Macro to check if IP is IPv4
 */
#define IS_IPV4(ip) (strchr(ip, '.') != NULL && strchr(ip, ':') == NULL)

/**
 * @brief Macro to check if IP is IPv6
 */
#define IS_IPV6(ip) (strchr(ip, ':') != NULL)

/**
 * @brief Macro to get default block duration in seconds
 */
#define DEFAULT_BLOCK_DURATION_MINUTES 5

/**
 * @brief Macro to get default rate limit per minute
 */
#define DEFAULT_RATE_LIMIT_PER_MINUTE 60

/**
 * @brief Macro to get default suspicious threshold
 */
#define DEFAULT_SUSPICIOUS_THRESHOLD 50

/**
 * @brief Macro to get default brute force threshold
 */
#define DEFAULT_BRUTE_FORCE_THRESHOLD 5

// ===== Version Information =====
#define FIREWALL_VERSION_MAJOR 2
#define FIREWALL_VERSION_MINOR 0
#define FIREWALL_VERSION_PATCH 0
#define FIREWALL_VERSION_STRING "2.0.0"

#endif // AIONIC_FIREWALL_H
