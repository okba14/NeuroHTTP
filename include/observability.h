#ifndef AIONIC_OBSERVABILITY_H
#define AIONIC_OBSERVABILITY_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    uint64_t total_requests;
    uint64_t total_responses;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t active_connections;
    uint64_t total_errors;
    uint64_t total_ai_calls;
    double avg_latency_ms;
    double p50_latency_ms;
    double p95_latency_ms;
    double p99_latency_ms;
    double uptime_seconds;
} ServerMetrics;

typedef struct {
    char request_id[32];
    const char *method;
    const char *path;
    int status_code;
    uint64_t latency_us;
    const char *model_name;
    uint64_t prompt_tokens;
    uint64_t completion_tokens;
    const char *provider;
    const char *client_ip;
    int cached;
    int error;
} RequestTrace;

typedef struct {
    char provider_name[64];
    uint64_t total_calls;
    uint64_t total_latency_us;
    uint64_t min_latency_us;
    uint64_t max_latency_us;
    uint64_t total_prompt_tokens;
    uint64_t total_completion_tokens;
    uint64_t errors;
    uint64_t last_call_time;
} ProviderStats;

typedef struct {
    uint64_t api_failures;
    uint64_t timeouts;
    uint64_t rate_limits;
    uint64_t decommissioned;
    uint64_t auth_errors;
    uint64_t invalid_model;
    uint64_t circuit_opens;
    uint64_t quarantined;
} ErrorBreakdown;

void obs_init(void);
void obs_trace_start(RequestTrace *trace, const char *request_id);
void obs_trace_end(RequestTrace *trace);
void obs_record_ai_call(const char *provider, const char *model, uint64_t latency_us, uint64_t prompt_tokens, uint64_t completion_tokens, int error, int error_code);
void obs_record_error_type(const char *provider, int error_code);
void obs_record_request(const char *method, const char *path, int status, uint64_t latency_us, int cached, int error);
void obs_get_metrics(ServerMetrics *metrics);
void obs_get_error_breakdown(ErrorBreakdown *eb);
int obs_get_provider_stats(const char *provider, ProviderStats *stats);
void obs_get_all_provider_stats(ProviderStats *stats, int *count, int max);
void obs_generate_request_id(char *buf, size_t len);
void obs_cleanup(void);

#endif
