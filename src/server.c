#define MAX_EVENTS 1024   // عدد الأحداث القصوى في epoll_wait
#define BACKLOG 128       // عدد الاتصالات المعلقة قبل accept
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <fcntl.h>  // ← هذا المهم لـ fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include "server.h"
#include "parser.h"
#include "router.h"
#include "stream.h"
#include "utils.h"

// تعريف بنية بيانات مؤشر الترابط
typedef struct {
    Server *server;
    int epoll_fd;
    int id;
} ThreadData;

// دالة مؤشر الترابط العاملة
static void *worker_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    Server *server = data->server;
    
    printf("Worker thread %d started\n", data->id);
    
    struct epoll_event events[MAX_EVENTS];
    
    while (server->active_connections > 0) {
        // انتظار الأحداث
        int event_count = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 100);
        
        if (event_count < 0) {
            if (errno == EINTR) continue;  // مقاطعة بواسطة إشارة
            perror("epoll_wait");
            break;
        }
        
        // معالجة الأحداث
        for (int i = 0; i < event_count; i++) {
            int client_fd = events[i].data.fd;
            
            if (events[i].events & EPOLLIN) {
                // بيانات جاهزة للقراءة
                if (server_handle_request(server, client_fd) != 0) {
                    // خطأ في معالجة الطلب، أغلق الاتصال
                    epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    server->active_connections--;
                }
            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                // خطأ في الاتصال أو إغلاقه
                epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                close(client_fd);
                server->active_connections--;
            }
        }
    }
    
    printf("Worker thread %d exiting\n", data->id);
    free(data);
    return NULL;
}

int server_init(Server *server, const Config *config) {
    memset(server, 0, sizeof(Server));
    
    server->port = config->port;
    server->thread_count = config->thread_count;
    server->max_connections = config->max_connections;
    
    // إنشاء مأخذ توصيل الخادم
    server->server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->server_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // تعيين خيارات المأخذ
    int opt = 1;
    if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server->server_fd);
        return -1;
    }
    
    // ربط المأخذ بالمنفذ
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);
    
    if (bind(server->server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server->server_fd);
        return -1;
    }
    
    // بدء الاستماع
    if (listen(server->server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server->server_fd);
        return -1;
    }
    
    // تهيئة مجمع مؤشرات الترابط
    server->thread_pool = malloc(sizeof(pthread_t) * server->thread_count);
    if (!server->thread_pool) {
        perror("malloc");
        close(server->server_fd);
        return -1;
    }
    
    // تهيئة طابور الطلبات
    server->request_queue = malloc(sizeof(void *) * server->max_connections);
    if (!server->request_queue) {
        perror("malloc");
        free(server->thread_pool);
        close(server->server_fd);
        return -1;
    }
    
    return 0;
}

int server_start(Server *server) {
    // إنشاء مؤشرات ترابط العاملين
    for (int i = 0; i < server->thread_count; i++) {
        ThreadData *data = malloc(sizeof(ThreadData));
        if (!data) {
            perror("malloc");
            continue;
        }
        
        data->server = server;
        data->id = i;
        
        // إنشاء مثيل epoll لكل مؤشر ترابط
        data->epoll_fd = epoll_create1(0);
        if (data->epoll_fd < 0) {
            perror("epoll_create1");
            free(data);
            continue;
        }
        
        if (pthread_create(&((pthread_t *)server->thread_pool)[i], NULL, worker_thread, data) != 0) {
            perror("pthread_create");
            close(data->epoll_fd);
            free(data);
            continue;
        }
    }
    
    return 0;
}

void server_cleanup(Server *server) {
    if (server->server_fd >= 0) {
        close(server->server_fd);
    }
    
    if (server->thread_pool) {
        free(server->thread_pool);
    }
    
    if (server->request_queue) {
        free(server->request_queue);
    }
}

int server_process_events(Server *server) {
    // قبول الاتصالات الجديدة
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (server->active_connections < server->max_connections) {
        int client_fd = accept(server->server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // لا المزيد من الاتصالات الجديدة
                break;
            }
            perror("accept");
            return -1;
        }
        
        // تعيين المأخذ كغير محجوب - الآن ستعمل بشكل صحيح
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        
        // توزيع الاتصال على أحد مؤشرات الترابط
        int thread_id = server->active_connections % server->thread_count;
        int epoll_fd = ((ThreadData *)((pthread_t *)server->thread_pool)[thread_id])->epoll_fd;
        
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;  // وضع الحافة
        event.data.fd = client_fd;
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
            perror("epoll_ctl");
            close(client_fd);
            return -1;
        }
        
        server->active_connections++;
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("New connection from %s:%d (fd: %d)\n", client_ip, ntohs(client_addr.sin_port), client_fd);
    }
    
    return 0;
}

int server_handle_request(Server *server, int client_fd) {
    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        return -1;  // خطأ أو إغلاق الاتصال
    }
    
    buffer[bytes_read] = '\0';  // تأكد من أن السلسلة منتهية
    
    // تحليل الطلب
    HTTPRequest request;
    if (parse_http_request(buffer, &request) != 0) {
        // خطأ في تحليل الطلب
        const char *error_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, error_response, strlen(error_response), 0);
        return -1;
    }
    
    // توجيه الطلب
    RouteResponse response;
    if (route_request(&request, &response) != 0) {
        // خطأ في التوجيه
        const char *error_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, error_response, strlen(error_response), 0);
        return -1;
    }
    
    // إرسال الاستجابة
    if (response.is_streaming) {
        // استجابة متدفقة
        stream_response(client_fd, &response);
    } else {
        // استجابة عادية
        send(client_fd, response.data, response.length, 0);
    }
    
    // تحديث الإحصائيات
    server->stats.total_requests++;
    server->stats.total_responses++;
    server->stats.bytes_sent += response.length;
    
    return 0;
}

int server_send_response(Server *server, int client_fd, const char *response, size_t length) {
    ssize_t bytes_sent = send(client_fd, response, length, 0);
    if (bytes_sent < 0) {
        perror("send");
        return -1;
    }
    
    server->stats.bytes_sent += bytes_sent;
    return 0;
}
