#ifndef AIONIC_OPTIMIZER_H
#define AIONIC_OPTIMIZER_H

#include "server.h"

// Constants for new features
#define PREDICTION_WINDOW_SIZE 10
#define HISTORY_COMPRESSION_THRESHOLD 50

typedef struct {
    time_t timestamp;
    double cpu_usage;
    double memory_usage;
    uint64_t requests_per_second;
    double avg_response_time;
    int active_connections;
    int thread_pool_utilization;
    // New metrics
    double network_usage;    // Network usage in MB/s
    double error_rate;       // Error rate in percentage
} PerformanceData;

// Core functions (unchanged)
int optimizer_init(const Config *config);
int optimizer_run(Server *server);
int optimizer_get_current_data(PerformanceData *data);
int optimizer_get_average_data(PerformanceData *data);
int optimizer_set_thresholds(double cpu_threshold, double memory_threshold, double response_time_threshold);
int optimizer_set_auto_optimization(int enable);
void optimizer_cleanup();

// New functions for enhanced features
int optimizer_set_advanced_thresholds(double cpu_threshold, double memory_threshold, 
                                     double response_time_threshold, double network_threshold,
                                     double error_rate_threshold);
int optimizer_set_prediction(int enable);
int optimizer_set_compression(int enable, int ratio);
int optimizer_set_prediction_logging(int interval, double change_threshold);
int optimizer_generate_report(char **report_buffer, int *report_size);

// Stub function declarations for missing dependencies
double get_server_error_rate(void);
int adjust_network_buffer_size(int adjustment);
int adjust_thread_pool_size(int adjustment);
int cache_increase_size(int percentage);
int cache_reduce_size(int percentage);

#endif // AIONIC_OPTIMIZER_H
