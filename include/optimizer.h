#ifndef AIONIC_OPTIMIZER_H
#define AIONIC_OPTIMIZER_H

#include "server.h"

// Constants
#define PREDICTION_WINDOW_SIZE 10
#define HISTORY_COMPRESSION_THRESHOLD 50

typedef struct {
    time_t timestamp;
    double cpu_usage;         // Percentage (0-100)
    double memory_usage;      // MB (RSS)
    uint64_t requests_per_second;
    double avg_response_time; // Milliseconds
    int active_connections;
    int thread_pool_utilization;
    double network_usage;    // Placeholder for future
    double error_rate;       // Placeholder for future
} PerformanceData;

// Core functions
int optimizer_init(const Config *config);
int optimizer_run(Server *server);
int optimizer_get_current_data(PerformanceData *data);
int optimizer_get_average_data(PerformanceData *data);
int optimizer_set_thresholds(double cpu_threshold, double memory_threshold, double response_time_threshold);
int optimizer_set_auto_optimization(int enable);
void optimizer_cleanup();

// Advanced features (Kept for API compatibility)
int optimizer_set_advanced_thresholds(double cpu, double mem, double resp, double net, double err);
int optimizer_set_prediction(int enable);
int optimizer_set_compression(int enable, int ratio);
int optimizer_set_prediction_logging(int interval, double change_threshold);
int optimizer_generate_report(char **report_buffer, int *report_size);
int optimizer_set_live_display(int enable);
int optimizer_show_performance();

// Dependency stubs (Kept for linking)
double get_server_error_rate(void);
int adjust_network_buffer_size(int adjustment);
int adjust_thread_pool_size(int adjustment);
int cache_increase_size(int percentage);
int cache_reduce_size(int percentage);

#endif // AIONIC_OPTIMIZER_H
