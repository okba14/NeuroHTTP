#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include "observability.h"
#include "ai/prompt_router.h"

#define MAX_PROVIDERS 64
#define MAX_TRACES 4096
#define LATENCY_BUCKETS 64

typedef struct {
    uint64_t count;
    uint64_t sum_us;
    uint64_t min_us;
    uint64_t max_us;
    atomic_uint_least64_t count_atomic;
    atomic_uint_least64_t sum_atomic;
} LatencyHistogram;

static struct {
    atomic_uint_least64_t total_requests;
    atomic_uint_least64_t total_responses;
    atomic_uint_least64_t bytes_sent;
    atomic_uint_least64_t bytes_received;
    atomic_uint_least64_t active_connections;
    atomic_uint_least64_t total_errors;
    atomic_uint_least64_t total_ai_calls;
    atomic_uint_least64_t total_prompt_tokens;
    atomic_uint_least64_t total_completion_tokens;
    pthread_mutex_t provider_mutex;
    ProviderStats providers[MAX_PROVIDERS];
    int provider_count;
    ErrorBreakdown error_breakdown;
    time_t start_time;
    int initialized;
} obs_state;

void obs_init(void) {
    if (obs_state.initialized) return;
    memset(&obs_state, 0, sizeof(obs_state));
    pthread_mutex_init(&obs_state.provider_mutex, NULL);
    obs_state.start_time = time(NULL);
    obs_state.initialized = 1;
}

void obs_generate_request_id(char *buf, size_t len) {
    static atomic_uint_least64_t counter = 0;
    uint64_t id = atomic_fetch_add(&counter, 1);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(buf, len, "%08x%04x%04x", (unsigned)(ts.tv_sec ^ ts.tv_nsec), (unsigned)(id >> 16), (unsigned)(id & 0xFFFF));
}

void obs_trace_start(RequestTrace *trace, const char *request_id) {
    if (!trace) return;
    memset(trace, 0, sizeof(RequestTrace));
    if (request_id) strncpy(trace->request_id, request_id, sizeof(trace->request_id) - 1);
}

void obs_trace_end(RequestTrace *trace) {
    if (!trace) return;
    obs_record_request(trace->method, trace->path, trace->status_code, trace->latency_us, trace->cached, trace->error);
}

void obs_record_error_type(const char *provider, int error_code) {
    (void)provider;
    switch (error_code) {
        case AI_ERROR_API_FAILURE:    atomic_fetch_add(&obs_state.error_breakdown.api_failures, 1); break;
        case AI_ERROR_TIMEOUT:        atomic_fetch_add(&obs_state.error_breakdown.timeouts, 1); break;
        case AI_ERROR_RATE_LIMIT:     atomic_fetch_add(&obs_state.error_breakdown.rate_limits, 1); break;
        case AI_ERROR_DECOMMISSIONED: atomic_fetch_add(&obs_state.error_breakdown.decommissioned, 1); break;
        case AI_ERROR_AUTH:           atomic_fetch_add(&obs_state.error_breakdown.auth_errors, 1); break;
        case AI_ERROR_INVALID_MODEL:  atomic_fetch_add(&obs_state.error_breakdown.invalid_model, 1); break;
        case AI_ERROR_QUARANTINED:    atomic_fetch_add(&obs_state.error_breakdown.quarantined, 1); break;
        case AI_ERROR_CIRCUIT_OPEN:   atomic_fetch_add(&obs_state.error_breakdown.circuit_opens, 1); break;
        default: break;
    }
}

void obs_record_ai_call(const char *provider, const char *model, uint64_t latency_us, uint64_t prompt_tokens, uint64_t completion_tokens, int error, int error_code) {
    atomic_fetch_add(&obs_state.total_ai_calls, 1);
    atomic_fetch_add(&obs_state.total_prompt_tokens, prompt_tokens);
    atomic_fetch_add(&obs_state.total_completion_tokens, completion_tokens);

    if (error) {
        atomic_fetch_add(&obs_state.total_errors, 1);
        obs_record_error_type(provider ? provider : model, error_code);
    }

    const char *provider_name = provider ? provider : model;
    if (!provider_name) return;

    pthread_mutex_lock(&obs_state.provider_mutex);
    int idx = -1;
    for (int i = 0; i < obs_state.provider_count; i++) {
        if (strcmp(obs_state.providers[i].provider_name, provider_name) == 0) { idx = i; break; }
    }
    if (idx < 0 && obs_state.provider_count < MAX_PROVIDERS) {
        idx = obs_state.provider_count++;
        strncpy(obs_state.providers[idx].provider_name, provider_name, sizeof(obs_state.providers[idx].provider_name) - 1);
        obs_state.providers[idx].min_latency_us = UINT64_MAX;
    }
    if (idx >= 0) {
        obs_state.providers[idx].total_calls++;
        obs_state.providers[idx].total_latency_us += latency_us;
        if (latency_us < obs_state.providers[idx].min_latency_us) obs_state.providers[idx].min_latency_us = latency_us;
        if (latency_us > obs_state.providers[idx].max_latency_us) obs_state.providers[idx].max_latency_us = latency_us;
        obs_state.providers[idx].total_prompt_tokens += prompt_tokens;
        obs_state.providers[idx].total_completion_tokens += completion_tokens;
        if (error) obs_state.providers[idx].errors++;
        obs_state.providers[idx].last_call_time = (uint64_t)time(NULL);
    }
    pthread_mutex_unlock(&obs_state.provider_mutex);
}

void obs_record_request(const char *method, const char *path, int status, uint64_t latency_us, int cached, int error) {
    (void)method; (void)path; (void)latency_us; (void)cached;
    atomic_fetch_add(&obs_state.total_requests, 1);
    atomic_fetch_add(&obs_state.total_responses, 1);
    if (error || status >= 400) atomic_fetch_add(&obs_state.total_errors, 1);
}

void obs_get_metrics(ServerMetrics *metrics) {
    if (!metrics) return;
    memset(metrics, 0, sizeof(ServerMetrics));
    metrics->total_requests = atomic_load(&obs_state.total_requests);
    metrics->total_responses = atomic_load(&obs_state.total_responses);
    metrics->bytes_sent = atomic_load(&obs_state.bytes_sent);
    metrics->bytes_received = atomic_load(&obs_state.bytes_received);
    metrics->active_connections = atomic_load(&obs_state.active_connections);
    metrics->total_errors = atomic_load(&obs_state.total_errors);
    metrics->total_ai_calls = atomic_load(&obs_state.total_ai_calls);
    metrics->uptime_seconds = difftime(time(NULL), obs_state.start_time);
}

void obs_get_error_breakdown(ErrorBreakdown *eb) {
    if (!eb) return;
    eb->api_failures = atomic_load(&obs_state.error_breakdown.api_failures);
    eb->timeouts = atomic_load(&obs_state.error_breakdown.timeouts);
    eb->rate_limits = atomic_load(&obs_state.error_breakdown.rate_limits);
    eb->decommissioned = atomic_load(&obs_state.error_breakdown.decommissioned);
    eb->auth_errors = atomic_load(&obs_state.error_breakdown.auth_errors);
    eb->invalid_model = atomic_load(&obs_state.error_breakdown.invalid_model);
    eb->circuit_opens = atomic_load(&obs_state.error_breakdown.circuit_opens);
    eb->quarantined = atomic_load(&obs_state.error_breakdown.quarantined);
}

int obs_get_provider_stats(const char *provider, ProviderStats *stats) {
    if (!provider || !stats) return -1;
    pthread_mutex_lock(&obs_state.provider_mutex);
    for (int i = 0; i < obs_state.provider_count; i++) {
        if (strcmp(obs_state.providers[i].provider_name, provider) == 0) {
            *stats = obs_state.providers[i];
            pthread_mutex_unlock(&obs_state.provider_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&obs_state.provider_mutex);
    return -1;
}

void obs_get_all_provider_stats(ProviderStats *stats, int *count, int max) {
    if (!stats || !count) return;
    pthread_mutex_lock(&obs_state.provider_mutex);
    *count = obs_state.provider_count < max ? obs_state.provider_count : max;
    for (int i = 0; i < *count; i++) stats[i] = obs_state.providers[i];
    pthread_mutex_unlock(&obs_state.provider_mutex);
}

void obs_cleanup(void) {
    obs_state.initialized = 0;
    pthread_mutex_destroy(&obs_state.provider_mutex);
}
