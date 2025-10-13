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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <string.h>
#include <stdlib.h>

// دالة مساعدة لتحويل سلسلة إلى أحرف صغيرة
static void to_lower_case(char *str) {
    for (; *str; ++str) *str = tolower(*str);
}

// دالة مساعدة لتحليل سطر الطلب
static int parse_request_line(const char *line, HTTPRequest *request) {
    char method[16];
    char path[1024];
    char version[16];
    
    if (sscanf(line, "%15s %1023s %15s", method, path, version) != 3) {
        return -1;
    }
    
    // تحديد نوع الطلب
    if (strcmp(method, "GET") == 0) request->method = HTTP_GET;
    else if (strcmp(method, "POST") == 0) request->method = HTTP_POST;
    else if (strcmp(method, "PUT") == 0) request->method = HTTP_PUT;
    else if (strcmp(method, "DELETE") == 0) request->method = HTTP_DELETE;
    else if (strcmp(method, "HEAD") == 0) request->method = HTTP_HEAD;
    else if (strcmp(method, "OPTIONS") == 0) request->method = HTTP_OPTIONS;
    else if (strcmp(method, "PATCH") == 0) request->method = HTTP_PATCH;
    else request->method = HTTP_UNKNOWN;
    
    // تحليل المسار وسلسلة الاستعلام
    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';  // فصل المسار عن سلسلة الاستعلام
        query++;
        request->query_string = strdup(query);
    } else {
        request->query_string = NULL;
    }
    
    request->path = strdup(path);
    
    return 0;
}

// دالة مساعدة لتحليل رأس HTTP
static int parse_header_line(const char *line, HTTPRequest *request) {
    char name[64];
    char value[512];
    
    // البحث عن نقطتين تفصلان اسم الرأس عن قيمته
    const char *colon = strchr(line, ':');
    if (!colon) return -1;
    
    size_t name_len = colon - line;
    if (name_len >= sizeof(name)) return -1;
    
    strncpy(name, line, name_len);
    name[name_len] = '\0';
    
    // تخطي المسافات بعد النقطتين
    const char *value_start = colon + 1;
    while (*value_start && isspace(*value_start)) value_start++;
    
    strncpy(value, value_start, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    
    // إزالة أحرف السطر الجديدة من النهاية
    size_t value_len = strlen(value);
    while (value_len > 0 && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n')) {
        value[--value_len] = '\0';
    }
    
    // تخزين الرأس
    if (request->header_count < 32) {
        char *header = malloc(strlen(name) + strlen(value) + 3);  // name: value\0
        if (header) {
            sprintf(header, "%s: %s", name, value);
            request->headers[request->header_count++] = header;
        }
    }
    
    return 0;
}

int parse_http_request(const char *raw_request, HTTPRequest *request) {
    if (!raw_request || !request) return -1;
    
    // تهيئة بنية الطلب
    memset(request, 0, sizeof(HTTPRequest));
    
    // نسخ الطلب الخام للمعالجة
    char *request_copy = strdup(raw_request);
    if (!request_copy) return -1;
    
    // تقسيم الطلب إلى أسطر
    char *line = strtok(request_copy, "\r\n");
    if (!line) {
        free(request_copy);
        return -1;
    }
    
    // تحليل سطر الطلب
    if (parse_request_line(line, request) != 0) {
        free(request_copy);
        return -1;
    }
    
    // تحليل الرؤوس
    while ((line = strtok(NULL, "\r\n")) && strlen(line) > 0) {
        parse_header_line(line, request);
    }
    
    // البحث عن نص الطلب (بعد سطر فارغ)
    char *body_start = strstr(raw_request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;  // تخطي \r\n\r\n
        size_t body_length = strlen(body_start);
        
        if (body_length > 0) {
            request->body = strdup(body_start);
            request->body_length = body_length;
        }
    }
    
    free(request_copy);
    return 0;
}

void free_http_request(HTTPRequest *request) {
    if (!request) return;
    
    if (request->path) free(request->path);
    if (request->query_string) free(request->query_string);
    if (request->body) free(request->body);
    
    for (int i = 0; i < request->header_count; i++) {
        if (request->headers[i]) free(request->headers[i]);
    }
    
    memset(request, 0, sizeof(HTTPRequest));
}

int parse_json(const char *json_string, void *output) {
    // استخدام دالة التجميع لتحليل JSON بسرعة
    if (!json_string || !output) return -1;
    
    // هنا سيتم استخدام دالة json_fast_tokenizer المكتوبة بـ Assembly
    json_fast_tokenizer(json_string, strlen(json_string));
    
    // للتبسيط، سنقوم بتحليل أساسي هنا
    // في التنفيذ الفعلي، سيتم استخدام دالة التجميع كاملة
    if (strstr(json_string, "\"prompt\"")) {
        char *prompt_start = strstr(json_string, "\"prompt\"");
        if (prompt_start) {
            prompt_start += strlen("\"prompt\"") + 1;  // تخطي "prompt":
            while (*prompt_start && (*prompt_start == ' ' || *prompt_start == ':')) prompt_start++;
            
            if (*prompt_start == '"') {
                prompt_start++;
                char *prompt_end = strchr(prompt_start, '"');
                if (prompt_end) {
                    size_t prompt_len = prompt_end - prompt_start;
                    strncpy((char *)output, prompt_start, prompt_len);
                    ((char *)output)[prompt_len] = '\0';
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

int json_get_value(const char *json_string, const char *key, char *output, size_t output_size) {
    if (!json_string || !key || !output || output_size == 0) return -1;
    
    // البحث عن المفتاح في سلسلة JSON
    char *key_pattern = malloc(strlen(key) + 4);  // "key":
    sprintf(key_pattern, "\"%s\"", key);
    
    char *key_start = strstr(json_string, key_pattern);
    if (!key_start) {
        free(key_pattern);
        return -1;
    }
    
    // الانتقال إلى بعد المفتاح
    key_start += strlen(key_pattern);
    
    // تخطي المسافات والنقطتين
    while (*key_start && (*key_start == ' ' || *key_start == ':')) key_start++;
    
    // استخراج القيمة
    if (*key_start == '"') {
        // قيمة نصية
        key_start++;
        char *value_end = strchr(key_start, '"');
        if (!value_end) {
            free(key_pattern);
            return -1;
        }
        
        size_t value_len = value_end - key_start;
        if (value_len >= output_size) {
            value_len = output_size - 1;
        }
        
        strncpy(output, key_start, value_len);
        output[value_len] = '\0';
    } else if (*key_start == '{' || *key_start == '[') {
        // كائن أو مصفوفة - معقدة، سنرجع خطأ للتبسيط
        free(key_pattern);
        return -1;
    } else {
        // قيمة رقمية أو منطقية
        char *value_end = key_start;
        while (*value_end && *value_end != ',' && *value_end != '}' && *value_end != ']' && 
               !isspace(*value_end)) {
            value_end++;
        }
        
        size_t value_len = value_end - key_start;
        if (value_len >= output_size) {
            value_len = output_size - 1;
        }
        
        strncpy(output, key_start, value_len);
        output[value_len] = '\0';
    }
    
    free(key_pattern);
    return 0;
}
