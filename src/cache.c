#define _POSIX_C_SOURCE 200809L

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "cache.h"
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <string.h>
#include <stdlib.h>

// تعريف بنئة التخزين المؤقت
typedef struct {
    CacheEntry *entries;
    int entry_count;
    int entry_capacity;
    pthread_mutex_t mutex;
    int default_ttl;
    int hits;
    int misses;
} Cache;

static Cache global_cache;

// دالة لحساب تجزئة بسيطة
static unsigned int simple_hash(const char *str) {
    unsigned int hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    
    return hash;
}

// دالة للعثور على عنصر في التخزين المؤقت
static CacheEntry *find_entry(const char *key) {
    if (!key || global_cache.entry_count == 0) {
        return NULL;
    }
    
    unsigned int hash = simple_hash(key);
    int index = hash % global_cache.entry_capacity;
    
    // البحث الخطي في حالة وجود تعارضات
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        int current_index = (index + i) % global_cache.entry_capacity;
        
        if (global_cache.entries[current_index].key && 
            strcmp(global_cache.entries[current_index].key, key) == 0) {
            return &global_cache.entries[current_index];
        }
        
        // إذا وجدنا خانة فارغة، فالعنصر غير موجود
        if (!global_cache.entries[current_index].key) {
            break;
        }
    }
    
    return NULL;
}

// دالة لإضافة عنصر جديد إلى التخزين المؤقت
static CacheEntry *add_entry(const char *key, const char *value, size_t value_size, int ttl) {
    // البحث عن خانة فارغة
    unsigned int hash = simple_hash(key);
    int index = hash % global_cache.entry_capacity;
    
    for (int i = 0; i < global_cache.entry_capacity; i++) {
        int current_index = (index + i) % global_cache.entry_capacity;
        
        if (!global_cache.entries[current_index].key) {
            // وجدنا خانة فارغة
            global_cache.entries[current_index].key = strdup(key);
            global_cache.entries[current_index].value = malloc(value_size);
            if (!global_cache.entries[current_index].value) {
                free(global_cache.entries[current_index].key);
                return NULL;
            }
            
            memcpy(global_cache.entries[current_index].value, value, value_size);
            global_cache.entries[current_index].value_size = value_size;
            global_cache.entries[current_index].timestamp = time(NULL);
            global_cache.entries[current_index].ttl = ttl > 0 ? ttl : global_cache.default_ttl;
            global_cache.entries[current_index].access_count = 0;
            
            global_cache.entry_count++;
            return &global_cache.entries[current_index];
        }
    }
    
    return NULL;  // التخزين المؤقت ممتلئ
}

// تهيئة التخزين المؤقت
int cache_init(int size, int ttl) {
    global_cache.entry_capacity = size;
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

// تعيين قيمة في التخزين المؤقت
int cache_set(const char *key, const char *value, size_t value_size, int ttl) {
    if (!key || !value || value_size == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&global_cache.mutex);
    
    // حذف العنصر القديم إذا كان موجوداً
    CacheEntry *existing_entry = find_entry(key);
    if (existing_entry) {
        free(existing_entry->key);
        free(existing_entry->value);
        global_cache.entry_count--;
    }
    
    // إضافة العنصر الجديد
    CacheEntry *entry = add_entry(key, value, value_size, ttl);
    if (!entry) {
        pthread_mutex_unlock(&global_cache.mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

// الحصول على قيمة من التخزين المؤقت
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
    
    // التحقق من انتهاء الصلاحية
    time_t current_time = time(NULL);
    if (current_time - entry->timestamp > entry->ttl) {
        // انتهت صلاحية العنصر، حذفه
        free(entry->key);
        free(entry->value);
        entry->key = NULL;
        entry->value = NULL;
        global_cache.entry_count--;
        
        global_cache.misses++;
        pthread_mutex_unlock(&global_cache.mutex);
        return -1;
    }
    
    // نسخ القيمة
    size_t copy_size = entry->value_size < value_size ? entry->value_size : value_size;
    memcpy(value, entry->value, copy_size);
    
    entry->access_count++;
    global_cache.hits++;
    
    pthread_mutex_unlock(&global_cache.mutex);
    return 0;
}

// حذف عنصر من التخزين المؤقت
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

// مسح التخزين المؤقت
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

// الحصول على إحصائيات التخزين المؤقت
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

// تنظيف التخزين المؤقت
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
