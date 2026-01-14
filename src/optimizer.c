#define _GNU_SOURCE  // Changed from _POSIX_C_SOURCE to fix implicit declarations

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <math.h>
#include <errno.h>
#include <malloc.h> 

// ===== Project Headers =====
#include "utils.h"
#include "optimizer.h"

// === Constants for Hysteresis Logic ===
#define HYSTERESIS_FACTOR 0.10  
#define STABILITY_WINDOW 3       

typedef struct {
    PerformanceData *history;
    int history_size;
    int history_capacity;
    pthread_mutex_t mutex;
    
    // Optimization State
    int enable_auto_optimization;
    time_t last_optimization_time;
    int optimization_interval;
    
    // Thresholds (Base values)
    double cpu_threshold;
    double memory_threshold_mb;
    double response_time_threshold;
    
    // Hysteresis State
    enum {
        OPTIMIZER_STATE_NORMAL,
        OPTIMIZER_STATE_OVERLOAD
    } state;
    
    int stability_counter; 
    

    int last_connection_count;
    double connection_spike_threshold;
    
} Optimizer;

static Optimizer global_optimizer;

// ===== REAL PROCESS METRICS FUNCTIONS =====


static double get_process_rss_mb() {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0.0;

    unsigned long size, resident, share, text, lib, data, dt;
    // Format: size resident share text lib data dt
    if (fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu", 
                &size, &resident, &share, &text, &lib, &data, &dt) != 7) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);

    // Resident is in pages. Page size is usually 4KB.
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) page_size = 4096;

    double rss_bytes = resident * page_size;
    return rss_bytes / (1024.0 * 1024.0); // Convert to MB
}


static double get_process_cpu_usage() {
    static struct rusage prev_usage;
    static struct timeval prev_time;
    
    struct rusage curr_usage;
    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);
    
    getrusage(RUSAGE_SELF, &curr_usage);
    
    // First call initialization
    if (prev_usage.ru_utime.tv_sec == 0 && prev_usage.ru_utime.tv_usec == 0) {
        prev_usage = curr_usage;
        prev_time = curr_time;
        return 0.0;
    }
    

    double user_time_diff = (curr_usage.ru_utime.tv_sec - prev_usage.ru_utime.tv_sec) +
                          (curr_usage.ru_utime.tv_usec - prev_usage.ru_utime.tv_usec) / 1000000.0;
    
    double sys_time_diff = (curr_usage.ru_stime.tv_sec - prev_usage.ru_stime.tv_sec) +
                         (curr_usage.ru_stime.tv_usec - prev_usage.ru_stime.tv_usec) / 1000000.0;
    
    double wall_time_diff = (curr_time.tv_sec - prev_time.tv_sec) +
                         (curr_time.tv_usec - prev_time.tv_usec) / 1000000.0;
    
    prev_usage = curr_usage;
    prev_time = curr_time;
    
    if (wall_time_diff <= 0.0) return 0.0;
    
    // Total CPU usage percentage for this process
    return ((user_time_diff + sys_time_diff) / wall_time_diff) * 100.0;
}

// ===== OPTIMIZATION LOGIC WITH HYSTERESIS =====

static void analyze_metrics(PerformanceData *data) {
    // 1. Check for Anomalies (Simplified WAF Logic)

    if (data->cpu_usage > (global_optimizer.cpu_threshold * 1.2) && 
        data->active_connections < global_optimizer.last_connection_count * 0.5 &&
        global_optimizer.last_connection_count > 5) {
        
        log_message("OPTIMIZER_WAF", "Anomaly Detected: High CPU with low traffic. Possible attack.");

    }
    global_optimizer.last_connection_count = data->active_connections;
    
    // 2. Hysteresis Check
    double upper_cpu = global_optimizer.cpu_threshold * (1.0 + HYSTERESIS_FACTOR);
    double lower_cpu = global_optimizer.cpu_threshold * (1.0 - HYSTERESIS_FACTOR);
    
    double upper_mem = global_optimizer.memory_threshold_mb * (1.0 + HYSTERESIS_FACTOR);
    double lower_mem = global_optimizer.memory_threshold_mb * (1.0 - HYSTERESIS_FACTOR);
    
    int is_overloaded = (data->cpu_usage > upper_cpu || 
                        data->memory_usage > upper_mem ||
                        data->avg_response_time > global_optimizer.response_time_threshold);
    
    int is_stable = (data->cpu_usage < lower_cpu && 
                     data->memory_usage < lower_mem &&
                     data->avg_response_time < global_optimizer.response_time_threshold);
    
    if (is_overloaded) {
        if (global_optimizer.state == OPTIMIZER_STATE_NORMAL) {
            global_optimizer.stability_counter++;
            if (global_optimizer.stability_counter >= STABILITY_WINDOW) {
                global_optimizer.state = OPTIMIZER_STATE_OVERLOAD;
                global_optimizer.stability_counter = 0;
                log_message("OPTIMIZER", "State changed: NORMAL -> OVERLOAD");
            }
        }
    } else if (is_stable) {
        if (global_optimizer.state == OPTIMIZER_STATE_OVERLOAD) {
            global_optimizer.stability_counter++;
            if (global_optimizer.stability_counter >= STABILITY_WINDOW) {
                global_optimizer.state = OPTIMIZER_STATE_NORMAL;
                global_optimizer.stability_counter = 0;
                log_message("OPTIMIZER", "State changed: OVERLOAD -> NORMAL");
            }
        }
    } else {

        global_optimizer.stability_counter = 0;
    }
}

static void trigger_optimization_actions() {
    if (global_optimizer.state != OPTIMIZER_STATE_OVERLOAD) return;
    
    PerformanceData current;
    optimizer_get_current_data(&current);
    
    log_message("OPTIMIZER", "Executing Hard Optimization Measures...");
    
    // 1. Memory Pressure
    if (current.memory_usage > global_optimizer.memory_threshold_mb) {
        log_message("OPTIMIZER", "Action: Memory Pressure Detected.");
        
        #ifdef __GLIBC__

        if (malloc_trim(0)) {
            log_message("OPTIMIZER", "Success: Released heap memory via malloc_trim");
        }
        #endif
        

        sync();
    }
    
    // 2. CPU Saturation
    if (current.cpu_usage > global_optimizer.cpu_threshold) {
        log_message("OPTIMIZER", "Action: CPU Saturation Detected. Lowering process priority.");
        

        if (nice(1) == -1 && errno != EPERM) {
             log_message("OPTIMIZER", "Warning: Failed to adjust nice value");
        }
    }
    
    // 3. Latency Spike (Response Time)
    if (current.avg_response_time > global_optimizer.response_time_threshold) {
        log_message("OPTIMIZER", "Action: High Latency Detected.");


        log_message("OPTIMIZER", "Suggestion: Increase Thread Pool Size or Enable Strict Rate Limiting.");
    }
}

// ===== PUBLIC API IMPLEMENTATION =====

int optimizer_init(const Config *config) {
    (void)config; // Config can be used to set initial thresholds

    global_optimizer.history_capacity = 100;
    global_optimizer.history = calloc(global_optimizer.history_capacity, sizeof(PerformanceData));
    if (!global_optimizer.history) return -1;

    global_optimizer.history_size = 0;
    
    // Default Thresholds 
    global_optimizer.cpu_threshold = 75.0;              // 75% of a single core
    global_optimizer.memory_threshold_mb = 256.0;         // 256 MB RSS
    global_optimizer.response_time_threshold = 500.0;      // 500ms
    
    // Hysteresis init
    global_optimizer.state = OPTIMIZER_STATE_NORMAL;
    global_optimizer.stability_counter = 0;
    global_optimizer.enable_auto_optimization = 1;
    global_optimizer.optimization_interval = 5; // Check every 5 seconds
    
    global_optimizer.last_connection_count = 0;

    if (pthread_mutex_init(&global_optimizer.mutex, NULL) != 0) {
        free(global_optimizer.history);
        return -1;
    }
    
    log_message("OPTIMIZER", "Advanced Optimizer initialized with Hysteresis & WAF-Anomaly Detection");
    return 0;
}

int optimizer_run(Server *server) {
    if (!global_optimizer.enable_auto_optimization) return 0;

    pthread_mutex_lock(&global_optimizer.mutex);

    time_t now = time(NULL);
    
    // Check interval to avoid busy loop
    if (now - global_optimizer.last_optimization_time < global_optimizer.optimization_interval) {
        pthread_mutex_unlock(&global_optimizer.mutex);
        return 0;
    }

    PerformanceData current;
    
    // 1. Collect REAL metrics
    current.timestamp = now;
    current.cpu_usage = get_process_cpu_usage();
    current.memory_usage = get_process_rss_mb();
    
    // 2. Get Server Context
    if (server) {
        current.active_connections = server->active_connections;
        current.requests_per_second = 0; // Should be calculated by server stats
        current.avg_response_time = server->stats.avg_response_time;
    } else {
        current.active_connections = 0;
        current.avg_response_time = 0;
    }

    // 3. Update History
    if (global_optimizer.history_size < global_optimizer.history_capacity) {
        global_optimizer.history[global_optimizer.history_size++] = current;
    } else {
        // Circular buffer shift
        memmove(&global_optimizer.history[0], &global_optimizer.history[1], 
                sizeof(PerformanceData) * (global_optimizer.history_capacity - 1));
        global_optimizer.history[global_optimizer.history_capacity - 1] = current;
    }


    analyze_metrics(&current);


    trigger_optimization_actions();

    global_optimizer.last_optimization_time = now;
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

int optimizer_get_current_data(PerformanceData *data) {
    if (!data) return -1;
    
    pthread_mutex_lock(&global_optimizer.mutex);
    if (global_optimizer.history_size > 0) {
        *data = global_optimizer.history[global_optimizer.history_size - 1];
    } else {

        data->timestamp = time(NULL);
        data->cpu_usage = get_process_cpu_usage();
        data->memory_usage = get_process_rss_mb();
    }
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

int optimizer_get_average_data(PerformanceData *data) {
    if (!data || global_optimizer.history_size == 0) return -1;
    
    pthread_mutex_lock(&global_optimizer.mutex);
    
    PerformanceData sum = {0};
    for (int i = 0; i < global_optimizer.history_size; i++) {
        sum.cpu_usage += global_optimizer.history[i].cpu_usage;
        sum.memory_usage += global_optimizer.history[i].memory_usage;
        sum.avg_response_time += global_optimizer.history[i].avg_response_time;
        sum.active_connections += global_optimizer.history[i].active_connections;
    }
    
    int count = global_optimizer.history_size;
    data->cpu_usage = sum.cpu_usage / count;
    data->memory_usage = sum.memory_usage / count;
    data->avg_response_time = sum.avg_response_time / count;
    data->active_connections = sum.active_connections / count;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

int optimizer_set_thresholds(double cpu_threshold, double memory_threshold, double response_time_threshold) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.cpu_threshold = cpu_threshold;
    global_optimizer.memory_threshold_mb = memory_threshold;
    global_optimizer.response_time_threshold = response_time_threshold;
    
    log_message("OPTIMIZER", "Thresholds updated via API");
    
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
    
    log_message("OPTIMIZER", "Advanced Optimizer cleaned up");
}

// ==== STUB IMPLEMENTATIONS (Left for compatibility) ====


double get_server_error_rate() {

    return 0.0;
}

int adjust_network_buffer_size(int adjustment) {

    (void)adjustment;
    return 0;
}

int adjust_thread_pool_size(int adjustment) {

    (void)adjustment;
    log_message("OPTIMIZER", "Info: Thread pool scaling requested (Stub)");
    return 0;
}

int cache_increase_size(int percentage) {
    (void)percentage;
    return 0;
}

int cache_reduce_size(int percentage) {
    (void)percentage;
    return 0;
}
