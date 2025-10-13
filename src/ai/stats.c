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
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "stats.h"
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <string.h>
#include <stdlib.h>

// تعريف بنئة إحصائيات النموذج
typedef struct {
    char *model_name;
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    double avg_response_time;
    double min_response_time;
    double max_response_time;
    uint64_t total_tokens_processed;
    time_t last_used;
} ModelStats;

// تعريف بنئة جامع الإحصائيات
typedef struct {
    ModelStats *model_stats;
    int model_count;
    int model_capacity;
    pthread_mutex_t mutex;
    char *stats_file;
    int auto_save_interval;
    time_t last_save_time;
} StatsCollector;

static StatsCollector global_stats;

// دالة لحفظ الإحصائيات إلى ملف
static int save_stats_to_file(const char *filename) {
    if (!filename) {
        return -1;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        return -1;
    }
    
    pthread_mutex_lock(&global_stats.mutex);
    
    // كتابة رأس JSON
    fprintf(file, "{\n");
    fprintf(file, "  \"models\": [\n");
    
    // كتابة إحصائيات كل نموذج
    for (int i = 0; i < global_stats.model_count; i++) {
        ModelStats *stats = &global_stats.model_stats[i];
        
        fprintf(file, "    {\n");
        fprintf(file, "      \"model_name\": \"%s\",\n", stats->model_name);
        fprintf(file, "      \"total_requests\": %lu,\n", stats->total_requests);
        fprintf(file, "      \"successful_requests\": %lu,\n", stats->successful_requests);
        fprintf(file, "      \"failed_requests\": %lu,\n", stats->failed_requests);
        fprintf(file, "      \"avg_response_time\": %.2f,\n", stats->avg_response_time);
        fprintf(file, "      \"min_response_time\": %.2f,\n", stats->min_response_time);
        fprintf(file, "      \"max_response_time\": %.2f,\n", stats->max_response_time);
        fprintf(file, "      \"total_tokens_processed\": %lu,\n", stats->total_tokens_processed);
        fprintf(file, "      \"last_used\": %ld\n", stats->last_used);
        fprintf(file, "    }%s\n", (i < global_stats.model_count - 1) ? "," : "");
    }
    
    // كتابة نهاية JSON
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");
    
    pthread_mutex_unlock(&global_stats.mutex);
    
    fclose(file);
    return 0;
}

// دالة لتحميل الإحصائيات من ملف
static int load_stats_from_file(const char *filename) {
    if (!filename) {
        return -1;
    }
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        return -1;
    }
    
    // قراءة الملف بالكامل
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *file_content = malloc(file_size + 1);
    if (!file_content) {
        fclose(file);
        return -1;
    }
    
    fread(file_content, 1, file_size, file);
    file_content[file_size] = '\0';
    
    fclose(file);
    
    // تحليل JSON (تبسيط)
    char *models_start = strstr(file_content, "\"models\":");
    if (!models_start) {
        free(file_content);
        return -1;
    }
    
    // في التنفيذ الفعلي، سيتم استخدام محلل JSON حقيقي هنا
    // للتبسيط، سنقوم بتحميل إحصائيات وهمية
    
    pthread_mutex_lock(&global_stats.mutex);
    
    // إضافة نموذج وهمي
    if (global_stats.model_count < global_stats.model_capacity) {
        ModelStats *stats = &global_stats.model_stats[global_stats.model_count];
        stats->model_name = strdup("gpt-3.5-turbo");
        stats->total_requests = 100;
        stats->successful_requests = 95;
        stats->failed_requests = 5;
        stats->avg_response_time = 250.5;
        stats->min_response_time = 120.0;
        stats->max_response_time = 800.0;
        stats->total_tokens_processed = 15000;
        stats->last_used = time(NULL);
        
        global_stats.model_count++;
    }
    
    pthread_mutex_unlock(&global_stats.mutex);
    
    free(file_content);
    return 0;
}

// تهيئة جامع الإحصائيات
int stats_init(const char *stats_file, int auto_save_interval) {
    global_stats.model_capacity = 16;
    global_stats.model_stats = calloc(global_stats.model_capacity, sizeof(ModelStats));
    if (!global_stats.model_stats) {
        return -1;
    }
    
    global_stats.model_count = 0;
    global_stats.stats_file = strdup(stats_file ? stats_file : "stats.json");
    global_stats.auto_save_interval = auto_save_interval;
    global_stats.last_save_time = time(NULL);
    
    if (pthread_mutex_init(&global_stats.mutex, NULL) != 0) {
        free(global_stats.model_stats);
        free(global_stats.stats_file);
        return -1;
    }
    
    // تحميل الإحصائيات من الملف إذا كان موجوداً
    load_stats_from_file(global_stats.stats_file);
    
    log_message("STATS", "Stats collector initialized");
    return 0;
}

// إضافة نموذج لتتبع إحصائياته
int stats_add_model(const char *model_name) {
    if (!model_name) {
        return -1;
    }
    
    pthread_mutex_lock(&global_stats.mutex);
    
    // التحقق من وجود النموذج بالفعل
    for (int i = 0; i < global_stats.model_count; i++) {
        if (strcmp(global_stats.model_stats[i].model_name, model_name) == 0) {
            pthread_mutex_unlock(&global_stats.mutex);
            return 0;  // موجود بالفعل
        }
    }
    
    // توسيع السعة إذا لزم الأمر
    if (global_stats.model_count >= global_stats.model_capacity) {
        int new_capacity = global_stats.model_capacity * 2;
        ModelStats *new_stats = realloc(global_stats.model_stats, 
                                       sizeof(ModelStats) * new_capacity);
        if (!new_stats) {
            pthread_mutex_unlock(&global_stats.mutex);
            return -1;
        }
        
        global_stats.model_stats = new_stats;
        global_stats.model_capacity = new_capacity;
    }
    
    // إضافة نموذج جديد
    ModelStats *stats = &global_stats.model_stats[global_stats.model_count];
    stats->model_name = strdup(model_name);
    stats->total_requests = 0;
    stats->successful_requests = 0;
    stats->failed_requests = 0;
    stats->avg_response_time = 0.0;
    stats->min_response_time = 0.0;
    stats->max_response_time = 0.0;
    stats->total_tokens_processed = 0;
    stats->last_used = time(NULL);
    
    global_stats.model_count++;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Added model to stats tracking: %s", model_name);
    log_message("STATS", log_msg);
    
    pthread_mutex_unlock(&global_stats.mutex);
    return 0;
}

// تسجيل طلب ناجح
int stats_record_successful_request(const char *model_name, double response_time, int token_count) {
    if (!model_name) {
        return -1;
    }
    
    pthread_mutex_lock(&global_stats.mutex);
    
    for (int i = 0; i < global_stats.model_count; i++) {
        if (strcmp(global_stats.model_stats[i].model_name, model_name) == 0) {
            ModelStats *stats = &global_stats.model_stats[i];
            
            stats->total_requests++;
            stats->successful_requests++;
            stats->total_tokens_processed += token_count;
            stats->last_used = time(NULL);
            
            // تحديث متوسط وقت الاستجابة
            if (stats->avg_response_time == 0.0) {
                stats->avg_response_time = response_time;
                stats->min_response_time = response_time;
                stats->max_response_time = response_time;
            } else {
                stats->avg_response_time = 
                    (stats->avg_response_time * (stats->successful_requests - 1) + response_time) / 
                    stats->successful_requests;
                
                if (response_time < stats->min_response_time) {
                    stats->min_response_time = response_time;
                }
                
                if (response_time > stats->max_response_time) {
                    stats->max_response_time = response_time;
                }
            }
            
            pthread_mutex_unlock(&global_stats.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_stats.mutex);
    return -1;
}

// تسجيل طلب فاشل
int stats_record_failed_request(const char *model_name) {
    if (!model_name) {
        return -1;
    }
    
    pthread_mutex_lock(&global_stats.mutex);
    
    for (int i = 0; i < global_stats.model_count; i++) {
        if (strcmp(global_stats.model_stats[i].model_name, model_name) == 0) {
            ModelStats *stats = &global_stats.model_stats[i];
            
            stats->total_requests++;
            stats->failed_requests++;
            stats->last_used = time(NULL);
            
            pthread_mutex_unlock(&global_stats.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_stats.mutex);
    return -1;
}

// الحصول على إحصائيات نموذج
int stats_get_model_stats(const char *model_name, ModelStats *output) {
    if (!model_name || !output) {
        return -1;
    }
    
    pthread_mutex_lock(&global_stats.mutex);
    
    for (int i = 0; i < global_stats.model_count; i++) {
        if (strcmp(global_stats.model_stats[i].model_name, model_name) == 0) {
            *output = global_stats.model_stats[i];
            
            // نسخ السلاسل لتجنب مشاكل الذاكرة
            output->model_name = strdup(global_stats.model_stats[i].model_name);
            
            pthread_mutex_unlock(&global_stats.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_stats.mutex);
    return -1;
}

// الحصول على إحصائيات جميع النماذج
int stats_get_all_stats(ModelStats **output, int *count) {
    if (!output || !count) {
        return -1;
    }
    
    pthread_mutex_lock(&global_stats.mutex);
    
    *count = global_stats.model_count;
    *output = malloc(sizeof(ModelStats) * (*count));
    
    if (!*output) {
        pthread_mutex_unlock(&global_stats.mutex);
        return -1;
    }
    
    for (int i = 0; i < *count; i++) {
        (*output)[i] = global_stats.model_stats[i];
        (*output)[i].model_name = strdup(global_stats.model_stats[i].model_name);
    }
    
    pthread_mutex_unlock(&global_stats.mutex);
    return 0;
}

// حفظ الإحصائيات تلقائياً
int stats_auto_save() {
    time_t current_time = time(NULL);
    
    if (current_time - global_stats.last_save_time >= global_stats.auto_save_interval) {
        if (save_stats_to_file(global_stats.stats_file) == 0) {
            global_stats.last_save_time = current_time;
            log_message("STATS", "Stats auto-saved to file");
            return 0;
        }
    }
    
    return -1;
}

// حفظ الإحصائيات يدوياً
int stats_save() {
    int result = save_stats_to_file(global_stats.stats_file);
    if (result == 0) {
        log_message("STATS", "Stats saved to file");
    }
    return result;
}

// تنظيف جامع الإحصائيات
void stats_cleanup() {
    pthread_mutex_lock(&global_stats.mutex);
    
    // حفظ الإحصائيات النهائية
    save_stats_to_file(global_stats.stats_file);
    
    // تحرير ذاكرة النماذج
    for (int i = 0; i < global_stats.model_count; i++) {
        free(global_stats.model_stats[i].model_name);
    }
    
    free(global_stats.model_stats);
    free(global_stats.stats_file);
    
    pthread_mutex_unlock(&global_stats.mutex);
    pthread_mutex_destroy(&global_stats.mutex);
    
    log_message("STATS", "Stats collector cleaned up");
}
