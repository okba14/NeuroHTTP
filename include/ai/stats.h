#ifndef AIONIC_AI_STATS_H
#define AIONIC_AI_STATS_H

#include <stdint.h>
#include <time.h>

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


int stats_init(const char *stats_file, int auto_save_interval);
int stats_add_model(const char *model_name);
int stats_record_successful_request(const char *model_name, double response_time, int token_count);
int stats_record_failed_request(const char *model_name);
int stats_get_model_stats(const char *model_name, ModelStats *output);
int stats_get_all_stats(ModelStats **output, int *count);
int stats_auto_save();
int stats_save();
void stats_cleanup();

#endif // AIONIC_AI_STATS_H
