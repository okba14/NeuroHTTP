#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "parser.h"
#include "firewall.h"
#include "config.h"

static const char *attack_patterns[] = {
    "GET / HTTP/1.1\r\nHost: test\r\n\r\n",
    "POST /api HTTP/1.1\r\nHost: test\r\nContent-Length: 5\r\n\r\nhello",
    "GET /../../../etc/passwd HTTP/1.1\r\nHost: test\r\n\r\n",
    "GET / HTTP/1.1\r\nHost: test\r\nUser-Agent: sqlmap\r\n\r\n",
    "GET / HTTP/1.1\r\nHost: test\r\nUser-Agent: nmap\r\n\r\n",
    "POST /v1/chat HTTP/1.1\r\nHost: test\r\nContent-Type: application/json\r\nContent-Length: 45\r\n\r\n{\"prompt\": \"test\", \"model\": \"test-model\"}",
    "GET / HTTP/1.1\r\nHost: test\r\nContent-Length: -1\r\n\r\n",
    "GET / HTTP/1.1\r\nHost: test\r\nContent-Length: 99999999999999999\r\n\r\n",
    NULL
};

static int test_http_parser_fuzz(void) {
    int passed = 0;
    for (int i = 0; attack_patterns[i]; i++) {
        HTTPRequest request;
        memset(&request, 0, sizeof(request));
        int ret = parse_http_request(attack_patterns[i], &request);
        (void)ret;
        free_http_request(&request);
        passed++;
    }
    printf("[PASS] test_http_parser_fuzz (%d patterns)\n", passed);
    return 0;
}

static int test_random_payloads(void) {
    srand((unsigned int)time(NULL));
    int passed = 0;
    for (int trial = 0; trial < 100; trial++) {
        size_t len = (size_t)(rand() % 2048);
        char *buf = malloc(len + 1);
        if (!buf) continue;
        for (size_t i = 0; i < len; i++) {
            buf[i] = (char)(rand() % 256);
        }
        buf[len] = '\0';

        HTTPRequest request;
        memset(&request, 0, sizeof(request));
        int ret = parse_http_request(buf, &request);
        (void)ret;
        free_http_request(&request);
        free(buf);
        passed++;
    }
    printf("[PASS] test_random_payloads (%d trials)\n", passed);
    return 0;
}

static int test_firewall_fuzz(void) {
    Config cfg = {0};
    cfg.max_connections = 1024;
    cfg.enable_firewall = 1;
    firewall_init(&cfg);

    srand((unsigned int)time(NULL));
    int passed = 0;
    for (int trial = 0; trial < 50; trial++) {
        char ip[20];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
                 rand() % 256, rand() % 256, rand() % 256, rand() % 256);
        int ret = firewall_check_request(ip, "test-key");
        (void)ret;
        passed++;
    }
    firewall_cleanup();
    printf("[PASS] test_firewall_fuzz (%d trials)\n", passed);
    return 0;
}

static int test_edge_cases(void) {
    HTTPRequest request;
    memset(&request, 0, sizeof(request));

    int ret = parse_http_request(NULL, &request);
    assert(ret != 0);

    ret = parse_http_request("", &request);
    assert(ret != 0);

    ret = parse_http_request("GET", &request);
    free_http_request(&request);

    printf("[PASS] test_edge_cases\n");
    return 0;
}

int main(void) {
    printf("=== Fuzz Tests ===\n");
    test_http_parser_fuzz();
    test_random_payloads();
    test_firewall_fuzz();
    test_edge_cases();
    printf("=== All fuzz tests PASSED ===\n");
    return 0;
}
