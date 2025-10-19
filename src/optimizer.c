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
#include <math.h> 

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
    // New fields for enhanced features
    double network_usage_threshold;      // Network usage threshold
    double error_rate_threshold;        // Error rate threshold
    int enable_prediction;              // Enable performance prediction
    int enable_compression;             // Enable history compression
    int compression_ratio;              // Compression ratio
    PerformanceData prediction_data;    // Prediction data
    // New fields for controlling message frequency
    time_t last_prediction_log_time;    // Last time prediction was logged
    int prediction_log_interval;        // Interval for logging predictions (seconds)
    double prediction_change_threshold;  // Threshold for significant prediction changes
    int prediction_line_printed;         // Flag to track if prediction line is printed
    PerformanceData last_displayed_pred; // Last displayed prediction values
    time_t last_prediction_update;      // Last time prediction display was updated
    int enable_live_display;            // Enable live display of predictions
} Optimizer;

static Optimizer global_optimizer;

// Enhanced CPU usage calculation
static double get_cpu_usage() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;
    
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", 
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
    fclose(fp);
    
    // Calculate total
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    unsigned long long busy = total - idle;
    
    static unsigned long long prev_total = 0, prev_busy = 0;
    unsigned long long diff_total = total - prev_total;
    unsigned long long diff_busy = busy - prev_busy;
    
    prev_total = total;
    prev_busy = busy;
    
    if (diff_total == 0) return 0.0;
    
    return (double)diff_busy * 100.0 / diff_total;
}

// Enhanced memory usage calculation
static double get_memory_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0.0;
    
    unsigned long long total_mem = 0, free_mem = 0, buffers = 0, cached = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %llu kB", &total_mem);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line, "MemFree: %llu kB", &free_mem);
        } else if (strncmp(line, "Buffers:", 8) == 0) {
            sscanf(line, "Buffers: %llu kB", &buffers);
        } else if (strncmp(line, "Cached:", 7) == 0) {
            sscanf(line, "Cached: %llu kB", &cached);
        }
    }
    
    fclose(fp);
    
    // Calculate actually used memory
    unsigned long long used_mem = total_mem - free_mem - buffers - cached;
    
    // Convert to MB
    return (double)used_mem / 1024.0;
}

// New function to get network usage
static double get_network_usage() {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return 0.0;
    
    char line[256];
    unsigned long long rx_bytes = 0, tx_bytes = 0;
    
    // Skip first two lines
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);
    
    // Read first network interface data (usually eth0 or ens33)
    if (fgets(line, sizeof(line), fp)) {
        char iface[16];
        sscanf(line, "%s %llu %*u %*u %*u %*u %*u %*u %*u %llu", 
               iface, &rx_bytes, &tx_bytes);
    }
    
    fclose(fp);
    
    // Calculate total bytes sent and received
    static unsigned long long prev_total = 0;
    unsigned long long total = rx_bytes + tx_bytes;
    
    // Calculate difference since last read
    unsigned long long diff = total - prev_total;
    prev_total = total;
    
    // Convert to MB/s
    return (double)diff / (1024 * 1024);
}

// New function to get error rate
static double get_error_rate() {
    // This function depends on project structure
    // We'll assume there's a function in another file to get error statistics
    extern double get_server_error_rate();
    return get_server_error_rate();
}

// Enhanced performance data collection
static void collect_performance_data(PerformanceData *data) {
    data->timestamp = time(NULL);
    data->cpu_usage = get_cpu_usage();
    data->memory_usage = get_memory_usage();
    data->requests_per_second = 0;
    data->avg_response_time = 0;
    data->active_connections = 0;
    data->thread_pool_utilization = 0;
    // New metrics
    data->network_usage = get_network_usage();
    data->error_rate = get_error_rate();
}

// Enhanced optimization check
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
    
    // Check new metrics
    if (data->network_usage > global_optimizer.network_usage_threshold) {
        return 1;
    }
    
    if (data->error_rate > global_optimizer.error_rate_threshold) {
        return 1;
    }
    
    // Check predictions if enabled
    if (global_optimizer.enable_prediction) {
        if (global_optimizer.prediction_data.cpu_usage > global_optimizer.cpu_threshold ||
            global_optimizer.prediction_data.memory_usage > global_optimizer.memory_threshold ||
            global_optimizer.prediction_data.avg_response_time > global_optimizer.response_time_threshold) {
            return 1;
        }
    }
    
    return 0;
}

// Enhanced memory optimization
static void optimize_memory() {
    log_message("OPTIMIZER", "Optimizing memory usage...");
    
    extern int cache_clear(void);
    cache_clear();
    
    #ifdef __GLIBC__
    extern int malloc_trim(size_t);
    malloc_trim(0);
    
    // Additional memory optimizations
    extern int cache_reduce_size(int);
    cache_reduce_size(25);  // Reduce cache size by 25%
    #endif
    
    log_message("OPTIMIZER", "Memory optimization completed");
}

// Enhanced CPU optimization
static void optimize_cpu() {
    log_message("OPTIMIZER", "Optimizing CPU usage...");
    
    #ifdef _GNU_SOURCE
    nice(1);
    #endif
    
    #ifdef __GLIBC__
    extern int malloc_trim(size_t);
    malloc_trim(0);
    
    // Additional CPU optimizations
    extern int adjust_thread_pool_size(int);
    adjust_thread_pool_size(-1);  // Reduce thread pool size
    #endif
    
    log_message("OPTIMIZER", "CPU optimization completed");
}

// Enhanced response time optimization
static void optimize_response_time() {
    log_message("OPTIMIZER", "Optimizing response time...");
    
    // Add optimizations for response time
    extern int adjust_thread_pool_size(int);
    adjust_thread_pool_size(1);   // Increase thread pool size
    
    extern int cache_increase_size(int);
    cache_increase_size(25);      // Increase cache size by 25%
    
    log_message("OPTIMIZER", "Response time optimization completed");
}

// New function to optimize network usage
static void optimize_network() {
    log_message("OPTIMIZER", "Optimizing network usage...");
    
    // Add optimizations for network
    extern int adjust_network_buffer_size(int);
    adjust_network_buffer_size(-1);  // Reduce network buffer size
    
    log_message("OPTIMIZER", "Network optimization completed");
}

// New function for performance prediction - DISABLED AUTO DISPLAY
static void predict_performance() {
    if (global_optimizer.history_size < PREDICTION_WINDOW_SIZE) {
        return;
    }
    
    // Use simple linear regression for prediction
    double sum_x = 0, sum_y_cpu = 0, sum_y_mem = 0, sum_y_resp = 0;
    double sum_xy_cpu = 0, sum_xy_mem = 0, sum_xy_resp = 0;
    double sum_x2 = 0;
    
    int n = PREDICTION_WINDOW_SIZE;
    int start_idx = global_optimizer.history_size - n;
    
    for (int i = 0; i < n; i++) {
        int idx = start_idx + i;
        sum_x += i;
        sum_y_cpu += global_optimizer.history[idx].cpu_usage;
        sum_y_mem += global_optimizer.history[idx].memory_usage;
        sum_y_resp += global_optimizer.history[idx].avg_response_time;
        sum_xy_cpu += i * global_optimizer.history[idx].cpu_usage;
        sum_xy_mem += i * global_optimizer.history[idx].memory_usage;
        sum_xy_resp += i * global_optimizer.history[idx].avg_response_time;
        sum_x2 += i * i;
    }
    
    // Calculate linear regression coefficients (y = a + bx)
    double denominator = n * sum_x2 - sum_x * sum_x;
    if (denominator == 0) return;
    
    double b_cpu = (n * sum_xy_cpu - sum_x * sum_y_cpu) / denominator;
    double a_cpu = (sum_y_cpu - b_cpu * sum_x) / n;
    
    double b_mem = (n * sum_xy_mem - sum_x * sum_y_mem) / denominator;
    double a_mem = (sum_y_mem - b_mem * sum_x) / n;
    
    double b_resp = (n * sum_xy_resp - sum_x * sum_y_resp) / denominator;
    double a_resp = (sum_y_resp - b_resp * sum_x) / n;
    
    // Predict future values
    int future = n;  // Predict same number of points as history
    global_optimizer.prediction_data.cpu_usage = a_cpu + b_cpu * future;
    global_optimizer.prediction_data.memory_usage = a_mem + b_mem * future;
    global_optimizer.prediction_data.avg_response_time = a_resp + b_resp * future;
    
    // Fix invalid values
    if (global_optimizer.prediction_data.cpu_usage < 0) {
        global_optimizer.prediction_data.cpu_usage = 0;
    }
    
    if (global_optimizer.prediction_data.memory_usage < 0) {
        global_optimizer.prediction_data.memory_usage = 0;
    }
    
    if (global_optimizer.prediction_data.avg_response_time < 0) {
        global_optimizer.prediction_data.avg_response_time = 0;
    }
    
    // Display prediction only if live display is enabled
    if (global_optimizer.enable_prediction && global_optimizer.enable_live_display) {
        time_t current_time = time(NULL);
        
        // Only update every 30 seconds at minimum
        if (current_time - global_optimizer.last_prediction_update >= 30) {
            printf("\r[OPTIMIZER] Performance: [CPU: %6.2f%%] [Memory: %8.2fMB] [Response: %6.2fms]   ", 
                   global_optimizer.prediction_data.cpu_usage,
                   global_optimizer.prediction_data.memory_usage,
                   global_optimizer.prediction_data.avg_response_time);
            fflush(stdout);
            
            global_optimizer.last_prediction_update = current_time;
            global_optimizer.prediction_line_printed = 1;
        }
    }
}

// New function to compress historical data
static void compress_history() {
    if (!global_optimizer.enable_compression || 
        global_optimizer.history_size < HISTORY_COMPRESSION_THRESHOLD) {
        return;
    }
    
    // Calculate compression ratio
    int new_size = global_optimizer.history_size / global_optimizer.compression_ratio;
    if (new_size < 10) new_size = 10;  // Keep minimum data points
    
    // Create new array for compressed data
    PerformanceData *compressed = calloc(new_size, sizeof(PerformanceData));
    if (!compressed) return;
    
    // Compress data by calculating averages
    int chunk_size = global_optimizer.history_size / new_size;
    for (int i = 0; i < new_size; i++) {
        int start = i * chunk_size;
        int end = (i == new_size - 1) ? global_optimizer.history_size : (i + 1) * chunk_size;
        
        PerformanceData sum = {0};
        for (int j = start; j < end; j++) {
            sum.cpu_usage += global_optimizer.history[j].cpu_usage;
            sum.memory_usage += global_optimizer.history[j].memory_usage;
            sum.requests_per_second += global_optimizer.history[j].requests_per_second;
            sum.avg_response_time += global_optimizer.history[j].avg_response_time;
            sum.active_connections += global_optimizer.history[j].active_connections;
            sum.thread_pool_utilization += global_optimizer.history[j].thread_pool_utilization;
            sum.network_usage += global_optimizer.history[j].network_usage;
            sum.error_rate += global_optimizer.history[j].error_rate;
        }
        
        // Calculate average
        int count = end - start;
        compressed[i].timestamp = global_optimizer.history[start + count/2].timestamp;  // Use middle timestamp
        compressed[i].cpu_usage = sum.cpu_usage / count;
        compressed[i].memory_usage = sum.memory_usage / count;
        compressed[i].requests_per_second = sum.requests_per_second / count;
        compressed[i].avg_response_time = sum.avg_response_time / count;
        compressed[i].active_connections = sum.active_connections / count;
        compressed[i].thread_pool_utilization = sum.thread_pool_utilization / count;
        compressed[i].network_usage = sum.network_usage / count;
        compressed[i].error_rate = sum.error_rate / count;
    }
    
    // Replace old data with compressed data
    free(global_optimizer.history);
    global_optimizer.history = compressed;
    global_optimizer.history_size = new_size;
    global_optimizer.history_capacity = new_size;
}

// Enhanced optimizer initialization
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
    global_optimizer.optimization_interval = 60;  // seconds
    global_optimizer.cpu_threshold = 80.0;  // 80%
    global_optimizer.memory_threshold = 512.0;  // 512MB
    global_optimizer.response_time_threshold = 1000.0;  // 1000ms
    global_optimizer.last_optimization_time = 0;
    
    // Initialize new fields
    global_optimizer.network_usage_threshold = 100.0;  // 100MB/s
    global_optimizer.error_rate_threshold = 5.0;       // 5%
    global_optimizer.enable_prediction = 1;           // Enable prediction
    global_optimizer.enable_compression = 1;           // Enable compression
    global_optimizer.compression_ratio = 2;            // Compress with 1:2 ratio
    
    // Initialize new fields for controlling message frequency
    global_optimizer.last_prediction_log_time = 0;
    global_optimizer.prediction_log_interval = 1800;   // Log predictions every 30 minutes
    global_optimizer.prediction_change_threshold = 15.0; // 15% change threshold
    global_optimizer.prediction_line_printed = 0;      // Flag to track if prediction line is printed
    global_optimizer.last_prediction_update = 0;      // Last time prediction display was updated
    global_optimizer.enable_live_display = 0;         // DISABLE live display by default
    memset(&global_optimizer.last_displayed_pred, 0, sizeof(PerformanceData));
    
    // Initialize prediction data
    memset(&global_optimizer.prediction_data, 0, sizeof(PerformanceData));
    
    if (pthread_mutex_init(&global_optimizer.mutex, NULL) != 0) {
        free(global_optimizer.history);
        return -1;
    }
    
    log_message("OPTIMIZER", "Optimizer initialized successfully");
    return 0;
}

// Enhanced optimizer run function
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
    
    // Add performance prediction
    if (global_optimizer.enable_prediction) {
        predict_performance();
    }
    
    // Add history compression
    if (global_optimizer.enable_compression) {
        compress_history();
    }
    
    if (current_time - global_optimizer.last_optimization_time >= global_optimizer.optimization_interval &&
        needs_optimization(&current_data)) {
        
        // If prediction line is printed, move to next line before logging optimization messages
        if (global_optimizer.prediction_line_printed) {
            printf("\n");
            fflush(stdout);
            global_optimizer.prediction_line_printed = 0;
        }
        
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
        
        // Add network optimization
        if (current_data.network_usage > global_optimizer.network_usage_threshold) {
            optimize_network();
        }
        
        global_optimizer.last_optimization_time = current_time;
        log_message("OPTIMIZER", "Optimization cycle completed");
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// Unchanged functions
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
        sum.network_usage += global_optimizer.history[i].network_usage;
        sum.error_rate += global_optimizer.history[i].error_rate;
    }
    
    data->cpu_usage = sum.cpu_usage / global_optimizer.history_size;
    data->memory_usage = sum.memory_usage / global_optimizer.history_size;
    data->requests_per_second = sum.requests_per_second / global_optimizer.history_size;
    data->avg_response_time = sum.avg_response_time / global_optimizer.history_size;
    data->active_connections = sum.active_connections / global_optimizer.history_size;
    data->thread_pool_utilization = sum.thread_pool_utilization / global_optimizer.history_size;
    data->network_usage = sum.network_usage / global_optimizer.history_size;
    data->error_rate = sum.error_rate / global_optimizer.history_size;
    
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

// New function to set advanced thresholds
int optimizer_set_advanced_thresholds(double cpu_threshold, double memory_threshold, 
                                     double response_time_threshold, double network_threshold,
                                     double error_rate_threshold) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.cpu_threshold = cpu_threshold;
    global_optimizer.memory_threshold = memory_threshold;
    global_optimizer.response_time_threshold = response_time_threshold;
    global_optimizer.network_usage_threshold = network_threshold;
    global_optimizer.error_rate_threshold = error_rate_threshold;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// New function to enable/disable prediction
int optimizer_set_prediction(int enable) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.enable_prediction = enable;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// New function to enable/disable compression
int optimizer_set_compression(int enable, int ratio) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.enable_compression = enable;
    if (ratio > 1) {
        global_optimizer.compression_ratio = ratio;
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// New function to control prediction logging frequency
int optimizer_set_prediction_logging(int interval, double change_threshold) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    if (interval > 0) {
        global_optimizer.prediction_log_interval = interval;
    }
    
    if (change_threshold > 0) {
        global_optimizer.prediction_change_threshold = change_threshold;
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// New function to enable/disable live display
int optimizer_set_live_display(int enable) {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    global_optimizer.enable_live_display = enable;
    if (enable) {
        global_optimizer.last_prediction_update = 0;  // Reset to force immediate update
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// New function to show current performance once
int optimizer_show_performance() {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    if (global_optimizer.enable_prediction) {
        printf("\n[OPTIMIZER] Current Performance: [CPU: %6.2f%%] [Memory: %8.2fMB] [Response: %6.2fms]\n", 
               global_optimizer.prediction_data.cpu_usage,
               global_optimizer.prediction_data.memory_usage,
               global_optimizer.prediction_data.avg_response_time);
        fflush(stdout);
    }
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

// New function to generate performance report
int optimizer_generate_report(char **report_buffer, int *report_size) {
    if (!report_buffer || !report_size) {
        return -1;
    }
    
    pthread_mutex_lock(&global_optimizer.mutex);
    
    // Calculate report size
    int estimated_size = 4096;  // Initial size
    char *buffer = malloc(estimated_size);
    if (!buffer) {
        pthread_mutex_unlock(&global_optimizer.mutex);
        return -1;
    }
    
    // Generate report
    int offset = 0;
    offset += snprintf(buffer + offset, estimated_size - offset, 
                      "=== Performance Report ===\n");
    offset += snprintf(buffer + offset, estimated_size - offset, 
                      "Generated at: %s", ctime(&(global_optimizer.history[global_optimizer.history_size - 1].timestamp)));
    
    // Add current performance data
    if (global_optimizer.history_size > 0) {
        PerformanceData current = global_optimizer.history[global_optimizer.history_size - 1];
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "\n=== Current Performance ===\n");
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "CPU Usage: %.2f%%\n", current.cpu_usage);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Memory Usage: %.2f MB\n", current.memory_usage);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Requests per Second: %lu\n", current.requests_per_second);  // Fixed format
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Average Response Time: %.2f ms\n", current.avg_response_time);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Active Connections: %d\n", current.active_connections);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Thread Pool Utilization: %d%%\n", current.thread_pool_utilization);  // Fixed format
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Network Usage: %.2f MB/s\n", current.network_usage);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Error Rate: %.2f%%\n", current.error_rate);
    }
    
    // Add predictions if enabled
    if (global_optimizer.enable_prediction) {
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "\n=== Performance Prediction ===\n");
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Predicted CPU Usage: %.2f%%\n", global_optimizer.prediction_data.cpu_usage);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Predicted Memory Usage: %.2f MB\n", global_optimizer.prediction_data.memory_usage);
        offset += snprintf(buffer + offset, estimated_size - offset, 
                          "Predicted Response Time: %.2f ms\n", global_optimizer.prediction_data.avg_response_time);
    }
    
    // Add recommendations
    offset += snprintf(buffer + offset, estimated_size - offset, 
                      "\n=== Recommendations ===\n");
    
    if (global_optimizer.history_size > 0) {
        PerformanceData current = global_optimizer.history[global_optimizer.history_size - 1];
        
        if (current.cpu_usage > global_optimizer.cpu_threshold * 0.8) {
            offset += snprintf(buffer + offset, estimated_size - offset, 
                              "- CPU usage is high. Consider adding more CPU resources or optimizing CPU-intensive tasks.\n");
        }
        
        if (current.memory_usage > global_optimizer.memory_threshold * 0.8) {
            offset += snprintf(buffer + offset, estimated_size - offset, 
                              "- Memory usage is high. Consider adding more memory or optimizing memory usage.\n");
        }
        
        if (current.avg_response_time > global_optimizer.response_time_threshold * 0.8) {
            offset += snprintf(buffer + offset, estimated_size - offset, 
                              "- Response time is high. Consider optimizing the application or adding more resources.\n");
        }
    }
    
    // Ensure buffer is sufficient
    if (offset >= estimated_size) {
        // Reallocate buffer if needed
        estimated_size = offset + 1;
        char *new_buffer = realloc(buffer, estimated_size);
        if (!new_buffer) {
            free(buffer);
            pthread_mutex_unlock(&global_optimizer.mutex);
            return -1;
        }
        buffer = new_buffer;
    }
    
    *report_buffer = buffer;
    *report_size = offset;
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    return 0;
}

void optimizer_cleanup() {
    pthread_mutex_lock(&global_optimizer.mutex);
    
    // If prediction line is printed, move to next line before cleanup
    if (global_optimizer.prediction_line_printed) {
        printf("\n");
        fflush(stdout);
    }
    
    free(global_optimizer.history);
    
    pthread_mutex_unlock(&global_optimizer.mutex);
    pthread_mutex_destroy(&global_optimizer.mutex);
    
    log_message("OPTIMIZER", "Optimizer cleaned up");
}

// Stub functions for missing dependencies
double get_server_error_rate() {
    return 0.0;  // Return 0% error rate for now
}

int adjust_network_buffer_size(int adjustment) {
    (void)adjustment;  // Avoid unused parameter warning
    return 0;  // Success
}

int adjust_thread_pool_size(int adjustment) {
    (void)adjustment;  // Avoid unused parameter warning
    return 0;  // Success
}

int cache_increase_size(int percentage) {
    (void)percentage;  // Avoid unused parameter warning
    return 0;  // Success
}

int cache_reduce_size(int percentage) {
    (void)percentage;  // Avoid unused parameter warning
    return 0;  // Success
}
