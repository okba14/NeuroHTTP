#ifndef AIONIC_RATELIMITER_H
#define AIONIC_RATELIMITER_H

#include <stddef.h>
#include <stdint.h>

typedef struct RateLimiter RateLimiter;

typedef struct {
    uint64_t tokens_per_sec;
    uint64_t bucket_size;
    uint64_t window_ms;
    uint64_t max_requests_per_window;
    uint64_t max_concurrent;
} TokenBucketConfig;

RateLimiter *ratelimiter_create(TokenBucketConfig cfg);
int ratelimiter_allow(RateLimiter *rl, const char *key);
int ratelimiter_allow_n(RateLimiter *rl, const char *key, uint64_t cost);
void ratelimiter_set_config(RateLimiter *rl, TokenBucketConfig cfg);
void ratelimiter_destroy(RateLimiter *rl);

#endif
