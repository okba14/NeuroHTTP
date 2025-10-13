#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <stdlib.h>
#include "router.h"
#include <string.h>
#include <stdlib.h>
#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include "stream.h"
#include <string.h>
#include <stdlib.h>
#include "ai/prompt_router.h"
#include <string.h>
#include <stdlib.h>

// قائمة المسارات المسجلة
static Route routes[64];
static int route_count = 0;

int route_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    // تهيئة الاستجابة
    memset(response, 0, sizeof(RouteResponse));
    response->status_code = 200;
    response->status_message = "OK";
    response->is_streaming = 0;
    
    // البحث عن معالج مناسب للمسار
    for (int i = 0; i < route_count; i++) {
        if (routes[i].method == request->method && 
            strcmp(routes[i].path, request->path) == 0) {
            // تنفيذ المعالج
            return routes[i].handler(request, response);
        }
    }
    
    // لم يتم العثور على مسار مطابق
    response->status_code = 404;
    response->status_message = "Not Found";
    response->data = strdup("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nNot Found");
    response->length = strlen(response->data);
    
    return 0;
}

void free_route_response(RouteResponse *response) {
    if (!response) return;
    
    if (response->data) free(response->data);
    if (response->stream_data) free(response->stream_data);
    
    for (int i = 0; i < response->header_count; i++) {
        if (response->headers[i]) free(response->headers[i]);
    }
    
    memset(response, 0, sizeof(RouteResponse));
}

int register_route(const char *path, HTTPMethod method, int (*handler)(HTTPRequest *, RouteResponse *)) {
    if (route_count >= 64) return -1;  // تم الوصول إلى الحد الأقصى للمسارات
    
    routes[route_count].path = strdup(path);
    routes[route_count].method = method;
    routes[route_count].handler = handler;
    route_count++;
    
    return 0;
}

int handle_chat_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    // استخراج الـ prompt من جسم الطلب
    char prompt[1024] = {0};
    if (request->body && parse_json(request->body, prompt) == 0) {
        printf("Received prompt: %s\n", prompt);
        
        // في التنفيذ الفعلي، سيتم إرسال الـ prompt إلى نموذج الذكاء الاصطناعي
        // هنا سنقوم بإنشاء استجابة بسيطة
        
        // تحديد ما إذا كان سيتم التدفق
        response->is_streaming = 0;  // سيتم تغيير هذا لاحقًا
        
        // إنشاء استجابة JSON
        char *json_response = malloc(4096);
        snprintf(json_response, 4096, 
                "{\"response\": \"Hello! You said: %s\", \"model\": \"aionic-1.0\"}", prompt);
        
        // إنشاء رأس HTTP
        char *http_response = malloc(strlen(json_response) + 256);
        snprintf(http_response, strlen(json_response) + 256, 
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
                strlen(json_response), json_response);
        
        response->data = http_response;
        response->length = strlen(http_response);
        
        free(json_response);
    } else {
        // خطأ في تحليل الطلب
        response->status_code = 400;
        response->status_message = "Bad Request";
        response->data = strdup("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nBad Request");
        response->length = strlen(response->data);
    }
    
    return 0;
}

int handle_stats_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    // في التنفيذ الفعلي، سيتم جلب الإحصائيات من الخادم
    char *stats_json = strdup("{\"requests\": 0, \"responses\": 0, \"uptime\": 0}");
    
    char *http_response = malloc(strlen(stats_json) + 256);
    snprintf(http_response, strlen(stats_json) + 256, 
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(stats_json), stats_json);
    
    response->data = http_response;
    response->length = strlen(http_response);
    
    free(stats_json);
    
    return 0;
}

int handle_health_request(HTTPRequest *request, RouteResponse *response) {
    if (!request || !response) return -1;
    
    response->data = strdup("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK");
    response->length = strlen(response->data);
    
    return 0;
}

// دالة تهيئة المسارات
void init_routes() {
    register_route("/v1/chat", HTTP_POST, handle_chat_request);
    register_route("/stats", HTTP_GET, handle_stats_request);
    register_route("/health", HTTP_GET, handle_health_request);
}
