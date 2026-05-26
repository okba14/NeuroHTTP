#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "ratelimiter.h"

#define RATE_HASH_SIZE 65536

typedef struct BucketEntry {
    char key[128];
    uint64_t tokens;
    uint64_t last_fill;
    uint64_t window_start;
    uint64_t window_count;
    int active;
    struct BucketEntry *next;
} BucketEntry;

struct RateLimiter {
    BucketEntry *buckets[RATE_HASH_SIZE];
    pthread_mutex_t mutex;
    TokenBucketConfig config;
};

static uint64_t hash_key(const char *key) {
    uint64_t h = 14695981039346656037ULL;
    while (*key) { h ^= (unsigned char)*key++; h *= 1099511628211ULL; }
    return h;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

RateLimiter *ratelimiter_create(TokenBucketConfig cfg) {
    RateLimiter *rl = calloc(1, sizeof(RateLimiter));
    if (!rl) return NULL;
    rl->config = cfg;
    if (rl->config.tokens_per_sec == 0) rl->config.tokens_per_sec = 100;
    if (rl->config.bucket_size == 0) rl->config.bucket_size = rl->config.tokens_per_sec;
    if (rl->config.window_ms == 0) rl->config.window_ms = 1000;
    if (rl->config.max_requests_per_window == 0) rl->config.max_requests_per_window = 1000;
    if (rl->config.max_concurrent == 0) rl->config.max_concurrent = 1024;
    pthread_mutex_init(&rl->mutex, NULL);
    return rl;
}

int ratelimiter_allow(RateLimiter *rl, const char *key) {
    return ratelimiter_allow_n(rl, key, 1);
}

int ratelimiter_allow_n(RateLimiter *rl, const char *key, uint64_t cost) {
    if (!rl || !key) return 0;
    uint64_t idx = hash_key(key) % RATE_HASH_SIZE;
    uint64_t now = now_ms();

    pthread_mutex_lock(&rl->mutex);

    BucketEntry *e = rl->buckets[idx];
    while (e) {
        if (e->active && strcmp(e->key, key) == 0) break;
        e = e->next;
    }

    if (!e) {
        if (cost > rl->config.bucket_size) { pthread_mutex_unlock(&rl->mutex); return 0; }
        e = calloc(1, sizeof(BucketEntry));
        if (!e) { pthread_mutex_unlock(&rl->mutex); return 0; }
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->tokens = rl->config.bucket_size - cost;
        e->last_fill = now;
        e->window_start = now;
        e->window_count = 1;
        e->active = 1;
        e->next = rl->buckets[idx];
        rl->buckets[idx] = e;
        pthread_mutex_unlock(&rl->mutex);
        return 1;
    }

    uint64_t elapsed = now - e->last_fill;
    uint64_t tokens_to_add = (elapsed * rl->config.tokens_per_sec) / 1000;
    if (tokens_to_add > 0) {
        e->tokens += tokens_to_add;
        if (e->tokens > rl->config.bucket_size) e->tokens = rl->config.bucket_size;
        e->last_fill = now;
    }

    if (now - e->window_start > rl->config.window_ms) {
        e->window_start = now;
        e->window_count = 0;
    }

    if (cost > e->tokens) { pthread_mutex_unlock(&rl->mutex); return 0; }
    if (e->window_count >= rl->config.max_requests_per_window) { pthread_mutex_unlock(&rl->mutex); return 0; }

    e->tokens -= cost;
    e->window_count++;
    pthread_mutex_unlock(&rl->mutex);
    return 1;
}

void ratelimiter_set_config(RateLimiter *rl, TokenBucketConfig cfg) {
    if (!rl) return;
    pthread_mutex_lock(&rl->mutex);
    rl->config = cfg;
    pthread_mutex_unlock(&rl->mutex);
}

void ratelimiter_destroy(RateLimiter *rl) {
    if (!rl) return;
    pthread_mutex_lock(&rl->mutex);
    for (int i = 0; i < RATE_HASH_SIZE; i++) {
        BucketEntry *e = rl->buckets[i];
        while (e) { BucketEntry *next = e->next; free(e); e = next; }
        rl->buckets[i] = NULL;
    }
    pthread_mutex_unlock(&rl->mutex);
    pthread_mutex_destroy(&rl->mutex);
    free(rl);
}
