#ifndef AIONIC_OPTIMIZER_H
#define AIONIC_OPTIMIZER_H

#include "server.h"


typedef struct {
    time_t timestamp;
    double cpu_usage;
    double memory_usage;
    uint64_t requests_per_second;
    double avg_response_time;
    int active_connections;
    int thread_pool_utilization;
} PerformanceData;


int optimizer_init(const Config *config);
int optimizer_run(Server *server);
int optimizer_get_current_data(PerformanceData *data);
int optimizer_get_average_data(PerformanceData *data);
int optimizer_set_thresholds(double cpu_threshold, double memory_threshold, double response_time_threshold);
int optimizer_set_auto_optimization(int enable);
void optimizer_cleanup();

#endif // AIONIC_OPTIMIZER_H
