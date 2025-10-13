#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/server.h"
#include "../include/config.h"
#include "../include/stream.h"

int test_streaming_response() {
    printf("Testing streaming response...\n");
    
    // إنشاء مقبس اختبار
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        printf("FAILED: Socket creation\n");
        return -1;
    }
    
    // ربط المقبس بمنفذ مؤقت
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = 0;  // اختيار منفذ تلقائي
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("FAILED: Bind socket\n");
        close(server_sock);
        return -1;
    }
    
    // الحصول على المنفذ الفعلي
    socklen_t len = sizeof(server_addr);
    if (getsockname(server_sock, (struct sockaddr *)&server_addr, &len) < 0) {
        printf("FAILED: Get socket name\n");
        close(server_sock);
        return -1;
    }
    
    int port = ntohs(server_addr.sin_port);
    
    // بدء الاستماع
    if (listen(server_sock, 1) < 0) {
        printf("FAILED: Listen\n");
        close(server_sock);
        return -1;
    }
    
    // إنشاء عميل اختبار
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) {
        printf("FAILED: Client socket creation\n");
        close(server_sock);
        return -1;
    }
    
    // الاتصال بالخادم
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);
    client_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(client_sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        printf("FAILED: Connect to server\n");
        close(client_sock);
        close(server_sock);
        return -1;
    }
    
    // قبول الاتصال من الخادم
    int accepted_sock = accept(server_sock, (struct sockaddr *)&server_addr, &len);
    if (accepted_sock < 0) {
        printf("FAILED: Accept connection\n");
        close(client_sock);
        close(server_sock);
        return -1;
    }
    
    // اختبار التدفق
    StreamData stream;
    if (stream_init(&stream, accepted_sock) != 0) {
        printf("FAILED: Stream initialization\n");
        close(accepted_sock);
        close(client_sock);
        close(server_sock);
        return -1;
    }
    
    // إرسال أجزاء البيانات
    const char *chunks[] = {"Hello, ", "streaming ", "world!"};
    for (int i = 0; i < sizeof(chunks)/sizeof(chunks[0]); i++) {
        if (stream_send_chunk(&stream, chunks[i], strlen(chunks[i])) != 0) {
            printf("FAILED: Stream send chunk\n");
            stream_cleanup(&stream);
            close(accepted_sock);
            close(client_sock);
            close(server_sock);
            return -1;
        }
    }
    
    // إنهاء التدفق
    if (stream_end(&stream) != 0) {
        printf("FAILED: Stream end\n");
        stream_cleanup(&stream);
        close(accepted_sock);
        close(client_sock);
        close(server_sock);
        return -1;
    }
    
    // استقبال البيانات من العميل
    char response[1024];
    int bytes_received = recv(client_sock, response, sizeof(response) - 1, 0);
    if (bytes_received < 0) {
        printf("FAILED: Receive response\n");
        stream_cleanup(&stream);
        close(accepted_sock);
        close(client_sock);
        close(server_sock);
        return -1;
    }
    
    response[bytes_received] = '\0';
    
    // التحقق من الاستجابة
    if (strstr(response, "200 OK") == NULL) {
        printf("FAILED: Invalid response\n");
        stream_cleanup(&stream);
        close(accepted_sock);
        close(client_sock);
        close(server_sock);
        return -1;
    }
    
    stream_cleanup(&stream);
    close(accepted_sock);
    close(client_sock);
    close(server_sock);
    
    printf("PASSED: Streaming response\n");
    return 0;
}

int main() {
    printf("Running streaming tests...\n");
    
    if (test_streaming_response() != 0) {
        printf("Streaming tests FAILED\n");
        return -1;
    }
    
    printf("All streaming tests PASSED\n");
    return 0;
}
