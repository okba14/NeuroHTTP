#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "firewall.h"
#include "config.h"

static int test_init(void) {
    Config cfg = {0};
    cfg.max_connections = 1024;
    cfg.enable_firewall = 1;
    int ret = firewall_init(&cfg);
    assert(ret == 0);
    printf("[PASS] test_firewall_init\n");
    return 0;
}

static int test_block_unblock(void) {
    int ret = firewall_block_ip("192.168.1.100");
    assert(ret == 0);

    char **blocked = NULL;
    int count = 0;
    ret = firewall_get_blocked_ips(&blocked, &count);
    assert(ret == 0);
    assert(count > 0);
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(blocked[i], "192.168.1.100") == 0) found = 1;
        free(blocked[i]);
    }
    free(blocked);
    assert(found);

    ret = firewall_unblock_ip("192.168.1.100");
    assert(ret == 0);

    printf("[PASS] test_block_unblock\n");
    return 0;
}

static int test_whitelist(void) {
    int ret = firewall_add_to_whitelist("10.0.0.1", 1, 3600);
    assert(ret == 0);

    int is_white = firewall_is_whitelisted("10.0.0.1");
    assert(is_white);

    ret = firewall_remove_from_whitelist("10.0.0.1");
    assert(ret == 0);

    is_white = firewall_is_whitelisted("10.0.0.1");
    assert(is_white == 0);

    printf("[PASS] test_whitelist\n");
    return 0;
}

static int test_blacklist(void) {
    int ret = firewall_add_to_blacklist("10.0.0.2", BLOCK_REASON_ATTACK_PATTERN, "test pattern");
    assert(ret == 0);

    int is_black = firewall_is_blacklisted("10.0.0.2");
    assert(is_black);

    ret = firewall_remove_from_blacklist("10.0.0.2");
    assert(ret == 0);

    printf("[PASS] test_blacklist\n");
    return 0;
}

static int test_stats(void) {
    FirewallStats stats;
    int ret = firewall_get_stats(&stats);
    assert(ret == 0);
    assert(stats.total_requests >= 0);

    printf("[PASS] test_stats\n");
    return 0;
}

static int test_rate_limits(void) {
    RateLimitConfig rl;
    int ret = firewall_get_rate_limit_config(&rl);
    assert(ret == 0);
    assert(rl.max_requests_per_minute > 0);

    rl.max_requests_per_minute = 100;
    ret = firewall_configure_rate_limits(&rl);
    assert(ret == 0);

    firewall_get_rate_limit_config(&rl);
    assert(rl.max_requests_per_minute == 100);

    printf("[PASS] test_rate_limits\n");
    return 0;
}

static int test_attack_patterns(void) {
    int ret = firewall_add_attack_pattern("<script", 9);
    assert(ret == 0);

    AttackPattern *patterns = NULL;
    int count = 0;
    ret = firewall_get_attack_patterns(&patterns, &count);
    assert(ret == 0);
    assert(count > 0);

    free(patterns);

    printf("[PASS] test_attack_patterns\n");
    return 0;
}

static int test_clear(void) {
    firewall_block_ip("10.0.0.3");
    int ret = firewall_clear_all();
    assert(ret == 0);

    FirewallStats stats;
    firewall_get_stats(&stats);
    assert(stats.blocked_requests == 0);

    printf("[PASS] test_clear\n");
    return 0;
}

int main(void) {
    printf("=== Firewall Tests ===\n");
    test_init();
    test_block_unblock();
    test_whitelist();
    test_blacklist();
    test_stats();
    test_rate_limits();
    test_attack_patterns();
    test_clear();
    firewall_cleanup();
    printf("=== All firewall tests PASSED ===\n");
    return 0;
}
