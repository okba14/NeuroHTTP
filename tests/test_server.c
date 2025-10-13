#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/server.h"
#include "../include/config.h"

int test_server_basic() {
    printf("Testing server basic functionality...\n");
    
    Config config;
    memset(&config, 0, sizeof(Config));
    config.port = 8080;
    config.thread_count = 2;
    config.max_connections = 10;
    
    Server server;
    if (server_init(&server, &config) != 0) {
        printf("FAILED: Server initialization\n");
        return -1;
    }
    
    if (server_start(&server) != 0) {
        printf("FAILED: Server start\n");
        server_cleanup(&server);
        return -1;
    }
    
    // اختبار اتصال بسيط
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("FAILED: Socket creation\n");
        server_stop(&server);
        server_cleanup(&server);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config.port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("FAILED: Connection to server\n");
        close(sock);
        server_stop(&server);
        server_cleanup(&server);
        return -1;
    }
    
    // إرسال طلب HTTP بسيط
    const char *request = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (send(sock, request, strlen(request), 0) < 0) {
        printf("FAILED: Send request\n");
        close(sock);
        server_stop(&server);
        server_cleanup(&server);
        return -1;
    }
    
    // استقبال الاستجابة
    char response[1024];
    int bytes_received = recv(sock, response, sizeof(response) - 1, 0);
    if (bytes_received < 0) {
        printf("FAILED: Receive response\n");
        close(sock);
        server_stop(&server);
        server_cleanup(&server);
        return -1;
    }
    
    response[bytes_received] = '\0';
    
    // التحقق من الاستجابة
    if (strstr(response, "200 OK") == NULL) {
        printf("FAILED: Invalid response\n");
        close(sock);
        server_stop(&server);
        server_cleanup(&server);
        return -1;
    }
    
    close(sock);
    server_stop(&server);
    server_cleanup(&server);
    
    printf("PASSED: Server basic functionality\n");
    return 0;
}

int main() {
    printf("Running server tests...\n");
    
    if (test_server_basic() != 0) {
        printf("Server tests FAILED\n");
        return -1;
    }
    
    printf("All server tests PASSED\n");
    return 0;
}
