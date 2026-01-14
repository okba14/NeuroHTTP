#define _GNU_SOURCE

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

// === Constants ===
#define HYSTERESIS_FACTOR 0.15  
#define STABILITY_WINDOW 3      
#define CORE_COUNT sysconf(_SC_NPROCESSORS_ONLN)

// === Control Levers (Internal Brain State) ===
typedef struct {
    int thread_pool_target_size;     // Desired thread count
    int waf_strictness_level;        // 0 (Permissive) to 10 (Strict)
    int cache_eviction_aggressiveness; // 0 (Keep) to 10 (Purge)
    int backpressure_level;          // 0 to 10
    int allocator_tuning_mode;       // 0: Normal, 1: Aggressive Trim
    int enable_streaming;            // 0 or 1
} SystemControlLevers;

typedef struct {
    PerformanceData *history;
    int history_size;
    int history_capacity;
    pthread_mutex_t mutex;
    
    // Configuration
    int enable_auto_optimization;
    time_t last_optimization_time;
    int optimization_interval;
    
    // Thresholds
    double cpu_threshold_per_core;
    double memory_threshold_mb;
    double response_time_threshold;
    
    // State Machine
    enum {
        OPTIMIZER_STATE_NORMAL,
        OPTIMIZER_STATE_OVERLOAD,
        OPTIMIZER_STATE_RECOVERY
    } state;
    
    int stability_counter; 
    
    // Anomaly Detection
    int last_connection_count;

    // Control Logic
    SystemControlLevers levers;

    // === Change Detection (Prevents redundant loops) ===
    int last_action_threads;
    int last_action_cache;

} Optimizer;

static Optimizer global_optimizer;

// ===== METRICS COLLECTION =====

static double get_process_rss_mb() {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0.0;

    unsigned long size, resident, share, text, lib, data, dt;
    if (fscanf(fp, "%lu %lu %lu %lu %lu %lu %lu", 
                &size, &resident, &share, &text, &lib, &data, &dt) != 7) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) page_size = 4096;

    return (resident * page_size) / (1024.0 * 1024.0);
}

static double get_process_cpu_usage() {
    static struct rusage prev_usage;
    static struct timeval prev_time;
    
    struct rusage curr_usage;
    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);
    getrusage(RUSAGE_SELF, &curr_usage);
    
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
    
    return ((user_time_diff + sys_time_diff) / wall_time_diff) * 100.0;
}

// ===== CORE LOGIC =====

static void analyze_metrics(PerformanceData *data) {
    // Detect Anomalies: High CPU + Low Traffic
    double expected_cpu_cap = global_optimizer.cpu_threshold_per_core * CORE_COUNT * 0.8;
    
    if (data->cpu_usage > expected_cpu_cap && 
        data->active_connections < global_optimizer.last_connection_count * 0.5 &&
        global_optimizer.last_connection_count > 10) {
        
        log_message("OPTIMIZER", "Anomaly Detected: High CPU / Low Traffic. Defensive Mode.");
        global_optimizer.levers.waf_strictness_level = 10;
        global_optimizer.levers.backpressure_level = 10;
    }
    global_optimizer.last_connection_count = data->active_connections;
    
    // State Machine Transitions
    double max_cpu = global_optimizer.cpu_threshold_per_core * CORE_COUNT;
    
    int is_overloaded = (data->cpu_usage > max_cpu || 
                        data->memory_usage > global_optimizer.memory_threshold_mb ||
                        data->avg_response_time > global_optimizer.response_time_threshold);
    
    int is_stable = (data->cpu_usage < (max_cpu * 0.7) && 
                     data->memory_usage < (global_optimizer.memory_threshold_mb * 0.9) &&
                     data->avg_response_time < global_optimizer.response_time_threshold);
    
    if (is_overloaded) {
        if (global_optimizer.state != OPTIMIZER_STATE_OVERLOAD) {
            global_optimizer.stability_counter++;
            if (global_optimizer.stability_counter >= STABILITY_WINDOW) {
                global_optimizer.state = OPTIMIZER_STATE_OVERLOAD;
                global_optimizer.stability_counter = 0;
                log_message("OPTIMIZER", "State -> OVERLOAD");
            }
        }
    } else if (is_stable) {
        if (global_optimizer.state == OPTIMIZER_STATE_OVERLOAD) {
            global_optimizer.stability_counter++;
            if (global_optimizer.stability_counter >= STABILITY_WINDOW) {
                global_optimizer.state = OPTIMIZER_STATE_RECOVERY;
                global_optimizer.stability_counter = 0;
                log_message("OPTIMIZER", "State -> RECOVERY");
            }
        } else if (global_optimizer.state == OPTIMIZER_STATE_RECOVERY) {
             global_optimizer.state = OPTIMIZER_STATE_NORMAL;
             log_message("OPTIMIZER", "State -> NORMAL");
        }
    } else {
        global_optimizer.stability_counter = 0;
    }
}

static void execute_control_actions(PerformanceData *current) {
    
    // 1. Memory Management
    if (current->memory_usage > global_optimizer.memory_threshold_mb) {
        global_optimizer.levers.cache_eviction_aggressiveness = 10;
        global_optimizer.levers.allocator_tuning_mode = 1;
        
        #ifdef __GLIBC__
        malloc_trim(0);
        if (current->memory_usage > global_optimizer.memory_threshold_mb * 1.2) {
             mallopt(M_MMAP_MAX, 0); 
        }
        #endif
        sync();
        
        // Only request reduction if state changed
        if (global_optimizer.last_action_cache != 1) {
            cache_reduce_size(20);
            global_optimizer.last_action_cache = 1;
            log_message("OPTIMIZER", "Action: Memory Trim & Cache Reduce");
        }
    } else if (current->memory_usage < (global_optimizer.memory_threshold_mb * 0.5)) {
        global_optimizer.levers.cache_eviction_aggressiveness = 0;
        global_optimizer.levers.allocator_tuning_mode = 0;
        #ifdef __GLIBC__
            mallopt(M_MMAP_MAX, 65536); 
        #endif
        
        if (global_optimizer.last_action_cache != 0) {
            cache_increase_size(10);
            global_optimizer.last_action_cache = 0;
        }
    }

    // 2. CPU & Thread Pool Strategy
    double load_avg[1];
    if (getloadavg(load_avg, 1) == 1) {
        int target_threads = 0;
        
        if (global_optimizer.state == OPTIMIZER_STATE_OVERLOAD) {
            // Survival Mode: Throttle threads to reduce context switching
            target_threads = (int)(CORE_COUNT * 1.5);
            global_optimizer.levers.waf_strictness_level = 3; // Relax security to save CPU
        } else {
            // Normal / Recovery Mode
            if (current->active_connections > 5) {
                // Traffic Burst: Scale up
                target_threads = (int)(CORE_COUNT * 4.0);
            } else {
                // Idle / Low Traffic: Conserve Resources (Scale down)
                target_threads = (int)(CORE_COUNT * 2.0);
            }
            global_optimizer.levers.waf_strictness_level = 8;
        }

        // Execute only if target changes
        if (target_threads > 0 && target_threads != global_optimizer.last_action_threads) {
            adjust_thread_pool_size(target_threads);
            global_optimizer.levers.thread_pool_target_size = target_threads;
            global_optimizer.last_action_threads = target_threads;
            
            // Log decision only on change
            char log_buf[64];
            snprintf(log_buf, sizeof(log_buf), "Threads: %d", target_threads);
            log_message("OPTIMIZER", log_buf);
        }
    }

    // 3. Latency Control
    if (current->avg_response_time > global_optimizer.response_time_threshold) {
        if (global_optimizer.levers.backpressure_level != 8) {
            global_optimizer.levers.backpressure_level = 8;
            global_optimizer.levers.enable_streaming = 1;
            log_message("OPTIMIZER", "Action: Backpressure Enabled");
        }
    } else {
        if (global_optimizer.levers.backpressure_level != 0) {
            global_optimizer.levers.backpressure_level = 0;
            global_optimizer.levers.enable_streaming = 0;
        }
    }
}

// ===== PUBLIC API =====

int optimizer_init(const Config *config) {
    (void)config;

    global_optimizer.history_capacity = 100;
    global_optimizer.history = calloc(global_optimizer.history_capacity, sizeof(PerformanceData));
    if (!global_optimizer.history) return -1;

    global_optimizer.history_size = 0;
    
    // Default Thresholds
    global_optimizer.cpu_threshold_per_core = 80.0;
    global_optimizer.memory_threshold_mb = 512.0;
    global_optimizer.response_time_threshold = 300.0;
    
    global_optimizer.state = OPTIMIZER_STATE_NORMAL;
    global_optimizer.stability_counter = 0;
    global_optimizer.enable_auto_optimization = 1;
    global_optimizer.optimization_interval = 5; 
    global_optimizer.last_connection_count = 0;

    // Initialize Levers
    global_optimizer.levers.thread_pool_target_size = (int)(CORE_COUNT * 2.0);
    global_optimizer.levers.waf_strictness_level = 7;
    global_optimizer.levers.cache_eviction_aggressiveness = 2;
    global_optimizer.levers.backpressure_level = 0;
    global_optimizer.levers.enable_streaming = 0;

    // Initialize Tracking (Set to -1 to force initial update)
    global_optimizer.last_action_threads = -1;
    global_optimizer.last_action_cache = -1;

    if (pthread_mutex_init(&global_optimizer.mutex, NULL) != 0) {
        free(global_optimizer.history);
        return -1;
    }
    
    log_message("OPTIMIZER", "Neural Optimizer v2.1 Initialized.");
    return 0;
}

int optimizer_run(Server *server) {
    if (!global_optimizer.enable_auto_optimization) return 0;

    pthread_mutex_lock(&global_optimizer.mutex);

    time_t now = time(NULL);
    if (now - global_optimizer.last_optimization_time < global_optimizer.optimization_interval) {
        pthread_mutex_unlock(&global_optimizer.mutex);
        return 0;
    }

    PerformanceData current;
    current.timestamp = now;
    current.cpu_usage = get_process_cpu_usage();
    current.memory_usage = get_process_rss_mb();
    
    if (server) {
        current.active_connections = server->active_connections;
        current.requests_per_second = 0; 
        current.avg_response_time = server->stats.avg_response_time;
    } else {
        current.active_connections = 0;
        current.avg_response_time = 0;
    }

    // Update History
    if (global_optimizer.history_size < global_optimizer.history_capacity) {
        global_optimizer.history[global_optimizer.history_size++] = current;
    } else {
        memmove(&global_optimizer.history[0], &global_optimizer.history[1], 
                sizeof(PerformanceData) * (global_optimizer.history_capacity - 1));
        global_optimizer.history[global_optimizer.history_capacity - 1] = current;
    }

    analyze_metrics(&current);
    execute_control_actions(&current);

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

int optimizer_get_waf_strictness() {
    pthread_mutex_lock(&global_optimizer.mutex);
    int level = global_optimizer.levers.waf_strictness_level;
    pthread_mutex_unlock(&global_optimizer.mutex);
    return level;
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
    global_optimizer.cpu_threshold_per_core = cpu_threshold;
    global_optimizer.memory_threshold_mb = memory_threshold;
    global_optimizer.response_time_threshold = response_time_threshold;
    log_message("OPTIMIZER", "Thresholds updated.");
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
    log_message("OPTIMIZER", "Optimizer shut down.");
}

// ==== STUBS ====

double get_server_error_rate() { return 0.0; }

int adjust_network_buffer_size(int adjustment) {
    (void)adjustment;
    return 0;
}

int adjust_thread_pool_size(int adjustment) {
    char log_buf[128];
    snprintf(log_buf, sizeof(log_buf), "Target Pool Size: %d", adjustment);
    log_message("OPTIMIZER", log_buf);
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
