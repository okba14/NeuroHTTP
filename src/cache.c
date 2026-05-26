#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include "utils.h"
#include "cache.h"
#include "asm_utils.h"

typedef struct {
    CacheEntry *entries;
    int entry_count;
    int entry_capacity;
    int capacity;
    pthread_mutex_t mutex;
    int default_ttl;
    int hits;
    int misses;
} Cache;

static Cache global_cache;

static uint32_t cache_hash(const char *key) {
    return crc32_asm(key, strlen(key));
}

static CacheEntry *find_entry(const char *key) {
    if (!key || global_cache.entry_count == 0) return NULL;
    unsigned int hash = cache_hash(key);
    int index = hash % global_cache.entry_capacity;
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        int ci = (index + i) % global_cache.entry_capacity;
        if (global_cache.entries[ci].key && strcmp(global_cache.entries[ci].key, key) == 0)
            return &global_cache.entries[ci];
        if (!global_cache.entries[ci].key) break;
    }
    return NULL;
}

static CacheEntry *add_entry(const char *key, const char *value, size_t value_size, int ttl) {
    unsigned int hash = cache_hash(key);
    int index = hash % global_cache.entry_capacity;
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        int ci = (index + i) % global_cache.entry_capacity;
        if (!global_cache.entries[ci].key) {
            global_cache.entries[ci].key = strdup(key);
            if (!global_cache.entries[ci].key) return NULL;
            global_cache.entries[ci].value = malloc(value_size);
            if (!global_cache.entries[ci].value) { free(global_cache.entries[ci].key); return NULL; }
            memcpy_dispatch(global_cache.entries[ci].value, value, value_size);
            global_cache.entries[ci].value_size = value_size;
            global_cache.entries[ci].timestamp = time(NULL);
            global_cache.entries[ci].ttl = ttl > 0 ? ttl : global_cache.default_ttl;
            global_cache.entries[ci].access_count = 0;
            global_cache.entry_count++;
            return &global_cache.entries[ci];
        }
    }
    return NULL;
}

int cache_put(Cache *cache, const char *key, const void *data, size_t size) {
    (void)cache; (void)key; (void)data; (void)size;
    return -1;
}

int cache_init(int size, int ttl) {
    global_cache.entry_capacity = size;
    global_cache.capacity = size;
    global_cache.entries = calloc(global_cache.entry_capacity, sizeof(CacheEntry));
    if (!global_cache.entries) return -1;
    global_cache.entry_count = 0;
    global_cache.default_ttl = ttl;
    global_cache.hits = 0;
    global_cache.misses = 0;
    if (pthread_mutex_init(&global_cache.mutex, NULL) != 0) { free(global_cache.entries); return -1; }
    log_message("CACHE", "Cache initialized");
    return 0;
}

int cache_set(const char *key, const char *value, size_t value_size, int ttl) {
    if (!key || !value || value_size == 0) return -1;
    pthread_mutex_lock(&global_cache.mutex);
    CacheEntry *existing = find_entry(key);
    if (existing) { free(existing->key); free(existing->value); global_cache.entry_count--; }
    CacheEntry *entry = add_entry(key, value, value_size, ttl);
    pthread_mutex_unlock(&global_cache.mutex);
    return entry ? 0 : -1;
}

int cache_get(const char *key, char *value, size_t value_size) {
    if (!key || !value || value_size == 0) return -1;
    pthread_mutex_lock(&global_cache.mutex);
    CacheEntry *entry = find_entry(key);
    if (!entry) { global_cache.misses++; pthread_mutex_unlock(&global_cache.mutex); return -1; }
    time_t now = time(NULL);
    if (now - entry->timestamp > entry->ttl) {
        free(entry->key); free(entry->value); entry->key = NULL; entry->value = NULL;
        global_cache.entry_count--; global_cache.misses++;
        pthread_mutex_unlock(&global_cache.mutex); return -1;
    }
    size_t copy = entry->value_size < value_size ? entry->value_size : value_size;
    memcpy_asm(value, entry->value, copy);
    entry->access_count++;
    global_cache.hits++;
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

int cache_delete(const char *key) {
    if (!key) return -1;
    pthread_mutex_lock(&global_cache.mutex);
    CacheEntry *entry = find_entry(key);
    if (entry) { free(entry->key); free(entry->value); entry->key = NULL; entry->value = NULL; global_cache.entry_count--; pthread_mutex_unlock(&global_cache.mutex); return 0; }
    pthread_mutex_unlock(&global_cache.mutex);
    return -1;
}

int cache_clear(void) {
    pthread_mutex_lock(&global_cache.mutex);
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        if (global_cache.entries[i].key) { free(global_cache.entries[i].key); free(global_cache.entries[i].value); global_cache.entries[i].key = NULL; global_cache.entries[i].value = NULL; }
    }
    global_cache.entry_count = 0;
    pthread_mutex_unlock(&global_cache.mutex);
    log_message("CACHE", "Cache cleared");
    return 0;
}

int cache_get_stats(int *entries, int *hits, int *misses) {
    if (!entries || !hits || !misses) return -1;
    pthread_mutex_lock(&global_cache.mutex);
    *entries = global_cache.entry_count;
    *hits = global_cache.hits;
    *misses = global_cache.misses;
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

void cache_cleanup(void) {
    pthread_mutex_lock(&global_cache.mutex);
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        if (global_cache.entries[i].key) { free(global_cache.entries[i].key); free(global_cache.entries[i].value); }
    }
    free(global_cache.entries);
    pthread_mutex_unlock(&global_cache.mutex);
    pthread_mutex_destroy(&global_cache.mutex);
}

static void normalize_prompt(const char *in, char *out, size_t out_size) {
    if (!in || !out || out_size == 0) return;
    size_t j = 0;
    int last_space = 0;
    for (const char *p = in; *p && j < out_size - 1; p++) {
        char c = tolower((unsigned char)*p);
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            if (!last_space) { out[j++] = ' '; last_space = 1; }
        } else if (ispunct((unsigned char)c)) {
            continue;
        } else { out[j++] = c; last_space = 0; }
    }
    if (j > 0 && out[j-1] == ' ') j--;
    out[j] = '\0';
}

static double compute_similarity(const char *a, const char *b) {
    if (!a || !b) return 0.0;
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0 || lb == 0) return 0.0;
    char norm_a[1024], norm_b[1024];
    normalize_prompt(a, norm_a, sizeof(norm_a));
    normalize_prompt(b, norm_b, sizeof(norm_b));
    if (strcmp(norm_a, norm_b) == 0) return 1.0;
    size_t nla = strlen(norm_a), nlb = strlen(norm_b);
    int max_gram = 3;
    int a_count = nla > (size_t)max_gram ? (int)(nla - max_gram + 1) : 1;
    int b_count = nlb > (size_t)max_gram ? (int)(nlb - max_gram + 1) : 1;
    int overlap = 0, total = a_count + b_count;
    for (int i = 0; i < a_count && (size_t)i + max_gram <= nla; i++) {
        for (int j = 0; j < b_count && (size_t)j + max_gram <= nlb; j++) {
            if (strncmp(norm_a + i, norm_b + j, max_gram) == 0) { overlap++; break; }
        }
    }
    if (total == 0) return 0.0;
    return (double)overlap / (double)(total - overlap > 0 ? total - overlap : total);
}

int cache_semantic_set(const char *prompt, const char *model, const char *value, size_t value_size, int ttl) {
    if (!prompt || !model || !value || value_size == 0) return -1;
    char norm_prompt[1024];
    normalize_prompt(prompt, norm_prompt, sizeof(norm_prompt));
    char cache_key[1152];
    snprintf(cache_key, sizeof(cache_key), "sem:%s:%s", model, norm_prompt);
    return cache_set(cache_key, value, value_size, ttl);
}

int cache_semantic_get(const char *prompt, const char *model, char *value, size_t value_size, double min_similarity) {
    if (!prompt || !model || !value || value_size == 0) return -1;
    char norm_prompt[1024];
    normalize_prompt(prompt, norm_prompt, sizeof(norm_prompt));
    pthread_mutex_lock(&global_cache.mutex);
    CacheEntry *best = NULL;
    double best_sim = 0.0;
    char best_key[1152];
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        if (!global_cache.entries[i].key) continue;
        if (strncmp(global_cache.entries[i].key, "sem:", 4) != 0) continue;
        const char *stored_prompt = global_cache.entries[i].key + 4;
        const char *colon = strchr(stored_prompt, ':');
        if (!colon) continue;
        const char *cached_prompt = colon + 1;
        double sim = compute_similarity(norm_prompt, cached_prompt);
        if (sim > best_sim) { best_sim = sim; best = &global_cache.entries[i]; strncpy(best_key, global_cache.entries[i].key, sizeof(best_key)-1); }
    }
    if (!best || best_sim < min_similarity) { pthread_mutex_unlock(&global_cache.mutex); return -1; }
    time_t now = time(NULL);
    if (now - best->timestamp > best->ttl) {
        best->key = NULL; best->value = NULL; global_cache.entry_count--;
        pthread_mutex_unlock(&global_cache.mutex); return -1;
    }
    size_t copy = best->value_size < value_size ? best->value_size : value_size;
    memcpy_asm(value, best->value, copy);
    best->access_count++;
    global_cache.hits++;
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}
