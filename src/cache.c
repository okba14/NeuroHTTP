#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "cache.h"
#include "utils.h"
#include "asm_utils.h"

// Cache storage structure
typedef struct {
    CacheEntry *entries;
    int entry_count;
    int entry_capacity;
    int capacity; // إضافة هذا الحقل الذي كان مفقوداً
    pthread_mutex_t mutex;
    int default_ttl;
    int hits;
    int misses;
} Cache;

static Cache global_cache;

// Function to compute hash using optimized CRC32
static uint32_t cache_hash(const char *key) {
    return crc32_asm(key, strlen(key));
}

// Function to find an entry in the cache
static CacheEntry *find_entry(const char *key) {
    if (!key || global_cache.entry_count == 0) {
        return NULL;
    }
    
    unsigned int hash = cache_hash(key);
    int index = hash % global_cache.entry_capacity;
    
    // Linear search in case of collisions
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        int current_index = (index + i) % global_cache.entry_capacity;
        
        if (global_cache.entries[current_index].key && 
            strcmp(global_cache.entries[current_index].key, key) == 0) {
            return &global_cache.entries[current_index];
        }
        
        // If we find an empty slot, the item doesn't exist
        if (!global_cache.entries[current_index].key) {
            break;
        }
    }
    
    return NULL;
}

// Function to add a new entry to the cache
static CacheEntry *add_entry(const char *key, const char *value, size_t value_size, int ttl) {
    // Find an empty slot
    unsigned int hash = cache_hash(key);
    int index = hash % global_cache.entry_capacity;
    
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        int current_index = (index + i) % global_cache.entry_capacity;
        
        if (!global_cache.entries[current_index].key) {
            // Found an empty slot
            global_cache.entries[current_index].key = strdup(key);
            global_cache.entries[current_index].value = malloc(value_size);
            if (!global_cache.entries[current_index].value) {
                free(global_cache.entries[current_index].key);
                return NULL;
            }
            
            // Use optimized memcpy to copy data
            memcpy_asm(global_cache.entries[current_index].value, value, value_size);
            global_cache.entries[current_index].value_size = value_size;
            global_cache.entries[current_index].timestamp = time(NULL);
            global_cache.entries[current_index].ttl = ttl > 0 ? ttl : global_cache.default_ttl;
            global_cache.entries[current_index].access_count = 0;
            
            global_cache.entry_count++;
            return &global_cache.entries[current_index];
        }
    }
    
    return NULL;  // Cache is full
}

// Function to put an item in the cache - تعديل هذه الدالة لتتوافق مع الهيكل الحالي
int cache_put(Cache *cache, const char *key, const void *data, size_t size) {
    uint32_t hash = cache_hash(key) % cache->capacity;
    
    // Check if we need to evict an item
    if (cache->entries[hash].key) {
        free(cache->entries[hash].key);
        free(cache->entries[hash].value);
    }
    
    // Allocate memory for the key
    cache->entries[hash].key = strdup(key);
    if (!cache->entries[hash].key) {
        return -1;
    }
    
    // Allocate memory for the data
    cache->entries[hash].value = malloc(size);
    if (!cache->entries[hash].value) {
        free(cache->entries[hash].key);
        return -1;
    }
    
    // Use optimized memcpy to copy the data
    memcpy_asm(cache->entries[hash].value, data, size);
    cache->entries[hash].value_size = size;
    cache->entries[hash].timestamp = time(NULL);
    
    return 0;
}

// Initialize the cache
int cache_init(int size, int ttl) {
    global_cache.entry_capacity = size;
    global_cache.capacity = size; // تهيئة الحقل الجديد
    global_cache.entries = calloc(global_cache.entry_capacity, sizeof(CacheEntry));
    if (!global_cache.entries) {
        return -1;
    }
    
    global_cache.entry_count = 0;
    global_cache.default_ttl = ttl;
    global_cache.hits = 0;
    global_cache.misses = 0;
    
    if (pthread_mutex_init(&global_cache.mutex, NULL) != 0) {
        free(global_cache.entries);
        return -1;
    }
    
    log_message("CACHE", "Cache initialized");
    return 0;
}

// Set a value in the cache
int cache_set(const char *key, const char *value, size_t value_size, int ttl) {
    if (!key || !value || value_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&global_cache.mutex);
    
    // Delete the old item if it exists
    CacheEntry *existing_entry = find_entry(key);
    if (existing_entry) {
        free(existing_entry->key);
        free(existing_entry->value);
        global_cache.entry_count--;
    }
    
    // Add the new item
    CacheEntry *entry = add_entry(key, value, value_size, ttl);
    if (!entry) {
        pthread_mutex_unlock(&global_cache.mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

// Get a value from the cache
int cache_get(const char *key, char *value, size_t value_size) {
    if (!key || !value || value_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&global_cache.mutex);
    
    CacheEntry *entry = find_entry(key);
    if (!entry) {
        global_cache.misses++;
        pthread_mutex_unlock(&global_cache.mutex);
        return -1;
    }
    
    // Check for expiration
    time_t current_time = time(NULL);
    if (current_time - entry->timestamp > entry->ttl) {
        // Item expired, delete it
        free(entry->key);
        free(entry->value);
        entry->key = NULL;
        entry->value = NULL;
        global_cache.entry_count--;
        
        global_cache.misses++;
        pthread_mutex_unlock(&global_cache.mutex);
        return -1;
    }
    
    // Copy the value
    size_t copy_size = entry->value_size < value_size ? entry->value_size : value_size;
    memcpy_asm(value, entry->value, copy_size);
    
    entry->access_count++;
    global_cache.hits++;
    
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

// Delete an item from the cache
int cache_delete(const char *key) {
    if (!key) {
        return -1;
    }
    
    pthread_mutex_lock(&global_cache.mutex);
    
    CacheEntry *entry = find_entry(key);
    if (entry) {
        free(entry->key);
        free(entry->value);
        entry->key = NULL;
        entry->value = NULL;
        global_cache.entry_count--;
        
        pthread_mutex_unlock(&global_cache.mutex);
        return 0;
    }
    
    pthread_mutex_unlock(&global_cache.mutex);
    return -1;
}

// Clear the cache
int cache_clear() {
    pthread_mutex_lock(&global_cache.mutex);
    
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        if (global_cache.entries[i].key) {
            free(global_cache.entries[i].key);
            free(global_cache.entries[i].value);
            global_cache.entries[i].key = NULL;
            global_cache.entries[i].value = NULL;
        }
    }
    
    global_cache.entry_count = 0;
    
    pthread_mutex_unlock(&global_cache.mutex);
    
    log_message("CACHE", "Cache cleared");
    return 0;
}

// Get cache statistics
int cache_get_stats(int *entries, int *hits, int *misses) {
    if (!entries || !hits || !misses) {
        return -1;
    }
    
    pthread_mutex_lock(&global_cache.mutex);
    
    *entries = global_cache.entry_count;
    *hits = global_cache.hits;
    *misses = global_cache.misses;
    
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

// Clean up the cache
void cache_cleanup() {
    pthread_mutex_lock(&global_cache.mutex);
    
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        if (global_cache.entries[i].key) {
            free(global_cache.entries[i].key);
            free(global_cache.entries[i].value);
        }
    }
    
    free(global_cache.entries);
    
    pthread_mutex_unlock(&global_cache.mutex);
    pthread_mutex_destroy(&global_cache.mutex);
    
    log_message("CACHE", "Cache cleaned up");
}
