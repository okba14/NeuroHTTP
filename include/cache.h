#ifndef AIONIC_CACHE_H
#define AIONIC_CACHE_H

#include <time.h>

typedef struct {
    char *key;
    char *value;
    size_t value_size;
    time_t timestamp;
    time_t ttl;
    int access_count;
} CacheEntry;

int cache_init(int size, int ttl);
int cache_set(const char *key, const char *value, size_t value_size, int ttl);
int cache_get(const char *key, char *value, size_t value_size);
int cache_delete(const char *key);
int cache_clear(void);
int cache_get_stats(int *entries, int *hits, int *misses);
void cache_cleanup(void);

int cache_semantic_get(const char *prompt, const char *model, char *value, size_t value_size, double min_similarity);
int cache_semantic_set(const char *prompt, const char *model, const char *value, size_t value_size, int ttl);

#endif
