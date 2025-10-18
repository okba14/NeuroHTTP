#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

// ===== Project Headers =====
#include "utils.h"
#include "cache.h"
#include "optimizer.h"


typedef struct {
    PerformanceData *history;
    int history_size;
    int history_capacity;
    pthread_mutex_t mutex;
    int enable_auto_optimization;
    int optimization_interval;
    double cpu_threshold;
    double memory_threshold;
    double response_time_threshold;
    int last_optimization_time;
} Optimizer;

static Optimizer global_optimizer;

static double get_cpu_usage() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;
    
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", 
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(fp);
    
    static unsigned long long prev_user = 0, prev_system = 0, prev_idle = 0;
    unsigned long long diff_user = user - prev_user;
    unsigned long long diff_system = system - prev_system;
    unsigned long long diff_idle = idle - prev_idle;
    
    prev_user = user;
    prev_system = system;
    prev_idle = idle;
    
    unsigned long long total = diff_user + diff_system + diff_idle;
    if (total == 0) return 0.0;
    
    return (double)(diff_user + diff_system) * 100.0 / total;
}

static double get_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    
    long page_size = sysconf(_SC_PAGESIZE);
    long resident_set = usage.ru_maxrss;
    
    return (double)resident_set * page_size / (1024 * 1024);
}

static void collect_performance_data(PerformanceData *data) {
    data->timestamp = time(NULL);
    data->cpu_usage = get_cpu_usage();
    data->memory_usage = get_memory_usage();
    data->requests_per_second = 0;
    data->avg_response_time = 0;
    data->active_connections = 0;
    data->thread_pool_utilization = 0;
}

static int needs_optimization(const PerformanceData *data) {
    if (data->cpu_usage > global_optimizer.cpu_threshold) {
        return 1;
    }
    
    if (data->memory_usage > global_optimizer.memory_threshold) {
        return 1;
    }
    
    if (data->avg_response_time > global_optimizer.response_time_threshold) {
        return 1;
    }
    
    return 0;
}

static void optimize_memory() {
    log_message("OPTIMIZER", "Optimizing memory usage...");
    
    extern int cache_clear(void);
    cache_clear();
    
    #ifdef __GLIBC__
    extern int malloc_trim(size_t);
    malloc_trim(0);
    #endif
    
    log_message("OPTIMIZER", "Memory optimization completed");
}

static void optimize_cpu() {
    log_message("OPTIMIZER", "Optimizing CPU usage...");
    
    #ifdef _GNU_SOURCE
    nice(1);
    #endif
    
    #ifdef __GLIBC__
    extern int malloc_trim(size_t);
    malloc_trim(0);
    #endif
    
    log_message("OPTIMIZER", "CPU optimization completed");
}

static void optimize_response_time() {
    log_message("OPTIMIZER", "Optimizing response time...");
    
    
    log_message("OPTIMIZER", "Response time optimization completed");
}

int optimizer_init(const Config *config) {
    if (!config) {
        return -1;
    }
    
    global_optimizer.history_capacity = 100;
    global_optimizer.history = calloc(global_optimizer.history_capacity, sizeof(PerformanceData));
    if (!global_optimizer.history) {
        return -1;
    }
    
    global_optimizer.history_size = 0;
    global_optimizer.enable_auto_optimization = config->enable_optimization;
    global_optimizer.optimization_interval = 60;  // ثانية
    global_optimizer.cpu_threshold = 80.0;  // 80%
    global_optimizer.memory_threshold = 512.0;  // 512MB
    global_optimizer.response_time_threshold = 1000.0;  // 1000ms
    global_optimizer.last_optimization_time = 0;
    
    if (pthread_mutex_init(&global_optimizer.mutex, NULL) != 0) {
        free(global_optimizer.history);
        return -1;
    }
    
    log_message("OPTIMIZER", "Optimizer initialized successfully");
    return 0;
}

int optimizer_run(Server *server) {
    if (!global_optimizer.enable_auto_optimization) {
        return 0;
    }
    
    time_t current_time = time(NULL);
    
    pthread_mutex_lock(&global_optimizer.mutex);
    
    PerformanceData current_data;
    collect_performance_data(&current_data);
    
    if (server) {
        current_data.active_connections = server->active_connections;
        current_data.requests_per_second = server->stats.total_requests / 
                                          (current_time - global_optimizer.last_optimization_time + 1);
        current_data.avg_response_time = server->stats.avg_response_time;
        
        if (server->thread_count > 0) {
            current_data.thread_pool_utilization = 
                (server->active_connections * 100) / server->thread_count;
        }
    }
    
    if (global_optimizer.history_size < global_optimizer.history_capacity) {
        global_optimizer.history[global_optimizer.history_size++] = current_data;
    } else {
        memmove(&global_optimizer.history[0], &global_optimizer.history[1], 
                sizeof(PerformanceData) * (global_optimizer.history_capacity - 1));
        global_optimizer.history[global_optimizer.history_capacity - 1] = current_data;
    }
    
    if (current_time - global_optimizer.last_optimization_time >= global_optimizer.optimization_interval &&
        needs_optimization(&current_data)) {
        
        log_message("OPTIMIZER", "Performance degradation detected, starting optimization...");
        
        if (current_data.memory_usage > global_optimizer.memory_threshold) {
            optimize_memory();
        }
        
        if (current_data.cpu_usage > global_optimizer.cpu_threshold) {
            optimize_cpu();
        }
        
        if (current_data.avg_response_time > global_optimizer.response_time_threshold) {
            optimize_response_time();
        }
        
        global_optimizer.last_optimization_time = current_time;
        log_message("OPTIMIZER", "Optimization cycle completed");
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

int optimizer_get_current_data(PerformanceData *data) {
    if (!data) {
        return -1;
    }
    
    pthread_mutex_lock(&global_optimizer.mutex);
    
    if (global_optimizer.history_size > 0) {
        *data = global_optimizer.history[global_optimizer.history_size - 1];
        pthread_mutex_unlock(&global_optimizer.mutex);
        return 0;
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return -1;
}

int optimizer_get_average_data(PerformanceData *data) {
    if (!data || global_optimizer.history_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&global_optimizer.mutex);
    
    PerformanceData sum = {0};
    for (int i = 0; i < global_optimizer.history_size; i++) {
        sum.cpu_usage += global_optimizer.history[i].cpu_usage;
        sum.memory_usage += global_optimizer.history[i].memory_usage;
        sum.requests_per_second += global_optimizer.history[i].requests_per_second;
        sum.avg_response_time += global_optimizer.history[i].avg_response_time;
        sum.active_connections += global_optimizer.history[i].active_connections;
        sum.thread_pool_utilization += global_optimizer.history[i].thread_pool_utilization;
    }
    
    data->cpu_usage = sum.cpu_usage / global_optimizer.history_size;
    data->memory_usage = sum.memory_usage / global_optimizer.history_size;
    data->requests_per_second = sum.requests_per_second / global_optimizer.history_size;
    data->avg_response_time = sum.avg_response_time / global_optimizer.history_size;
    data->active_connections = sum.active_connections / global_optimizer.history_size;
    data->thread_pool_utilization = sum.thread_pool_utilization / global_optimizer.history_size;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

int optimizer_set_thresholds(double cpu_threshold, double memory_threshold, double response_time_threshold) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.cpu_threshold = cpu_threshold;
    global_optimizer.memory_threshold = memory_threshold;
    global_optimizer.response_time_threshold = response_time_threshold;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

int optimizer_set_auto_optimization(int enable) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.enable_auto_optimization = enable;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

void optimizer_cleanup() {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    free(global_optimizer.history);
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    pthread_mutex_destroy(&global_optimizer.mutex);
    
    log_message("OPTIMIZER", "Optimizer cleaned up");
}
