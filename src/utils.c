#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>

// ===== Project Headers =====
#include "utils.h"
#include "asm_utils.h"

// ===== Constants =====
#define MAX_LOG_MESSAGE_SIZE 4096
#define DEFAULT_BUFFER_SIZE 8192
#define CRC32_POLYNOMIAL 0xEDB88320

// ===== Thread-Safe Logging =====
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file = NULL;
static atomic_int log_level = ATOMIC_VAR_INIT(LOG_LEVEL_INFO);

// ===== Error Handling Macros =====
#define SAFE_FREE(ptr) do { \
    if (ptr) { \
        free(ptr); \
        ptr = NULL; \
    } \
} while(0)

#define CHECK_NULL_RETURN(ptr, ret) do { \
    if (!(ptr)) { \
        log_message_ex(LOG_LEVEL_ERROR, "NULL pointer at %s:%d", __FILE__, __LINE__); \
        return ret; \
    } \
} while(0)

// ===== Improved File Reading with Error Handling =====
char *read_file_ex(const char *filename, size_t *out_size, int *error_code) {
    CHECK_NULL_RETURN(filename, NULL);
    
    if (error_code) *error_code = 0;
    if (out_size) *out_size = 0;
    
    // Check file existence and permissions
    if (access(filename, R_OK) != 0) {
        if (error_code) *error_code = errno;
        return NULL;
    }
    
    FILE *file = fopen(filename, "rb");  // Binary mode for consistency
    if (!file) {
        if (error_code) *error_code = errno;
        return NULL;
    }
    
    // Get file size safely
    if (fseek(file, 0, SEEK_END) != 0) {
        if (error_code) *error_code = errno;
        fclose(file);
        return NULL;
    }
    
    long file_size = ftell(file);
    if (file_size < 0 || file_size > INT_MAX) {  // Sanity check
        if (error_code) *error_code = EFBIG;
        fclose(file);
        return NULL;
    }
    
    rewind(file);
    
    // Allocate with extra space for safety
    char *content = calloc(1, file_size + 1);
    if (!content) {
        if (error_code) *error_code = ENOMEM;
        fclose(file);
        return NULL;
    }
    
    // Read with verification
    size_t read_size = fread(content, 1, file_size, file);
    if (read_size != (size_t)file_size) {
        if (error_code) *error_code = EIO;
        SAFE_FREE(content);
        fclose(file);
        return NULL;
    }
    
    content[file_size] = '\0';
    if (out_size) *out_size = file_size;
    
    fclose(file);
    return content;
}

// Wrapper for backward compatibility
char *read_file(const char *filename) {
    return read_file_ex(filename, NULL, NULL);
}

// ===== High-Resolution Timing =====
uint64_t get_current_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t get_current_time_us(void) {
    return get_current_time_ns() / 1000;
}

uint64_t get_current_time_ms(void) {
    return get_current_time_ns() / 1000000;
}

// ===== Thread-Safe Random String Generation =====
void generate_random_string_safe(char *buffer, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static __thread unsigned int seed = 0;
    
    CHECK_NULL_RETURN(buffer, );
    if (length == 0) return;
    
    // Initialize seed once per thread
    if (seed == 0) {
        seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self();
    }
    
    for (size_t i = 0; i < length - 1; i++) {
        buffer[i] = charset[rand_r(&seed) % (sizeof(charset) - 1)];
    }
    
    buffer[length - 1] = '\0';
}

// Wrapper for compatibility
void generate_random_string(char *buffer, size_t length) {
    generate_random_string_safe(buffer, length);
}

// ===== Enhanced String to Integer Conversion =====
int string_to_int_ex(const char *str, int *result, int base) {
    CHECK_NULL_RETURN(str, -1);
    CHECK_NULL_RETURN(result, -1);
    
    // Skip whitespace
    while (isspace(*str)) str++;
    
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, base);
    
    // Check for errors
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
        return -2;  // Overflow
    }
    
    if (endptr == str || *endptr != '\0') {
        return -1;  // Invalid conversion
    }
    
    *result = (int)val;
    return 0;
}

// Wrapper for compatibility
int string_to_int(const char *str, int *result) {
    return string_to_int_ex(str, result, 10);
}

// ===== Enhanced Logging System =====
void init_logging(const char *log_filename, log_level_t level) {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file && log_file != stdout && log_file != stderr) {
        fclose(log_file);
    }
    
    if (log_filename) {
        log_file = fopen(log_filename, "a");
        if (!log_file) {
            log_file = stderr;
        }
    } else {
        log_file = stdout;
    }
    
    atomic_store(&log_level, level);
    pthread_mutex_unlock(&log_mutex);
}

void log_message_ex(log_level_t level, const char *format, ...) {
    if (level < (log_level_t)atomic_load(&log_level)) return;
    
    const char *level_str[] = {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"};
    
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    FILE *output = log_file ? log_file : stdout;
    
    fprintf(output, "[%s] [%-8s] ", time_str, level_str[level]);
    
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    fprintf(output, "\n");
    fflush(output);
    
    pthread_mutex_unlock(&log_mutex);
}

// Wrapper for compatibility
void log_message(const char *level, const char *message) {
    log_level_t log_lvl = LOG_LEVEL_INFO;
    
    if (strcmp(level, "DEBUG") == 0) log_lvl = LOG_LEVEL_DEBUG;
    else if (strcmp(level, "WARNING") == 0) log_lvl = LOG_LEVEL_WARNING;
    else if (strcmp(level, "ERROR") == 0) log_lvl = LOG_LEVEL_ERROR;
    else if (strcmp(level, "CRITICAL") == 0) log_lvl = LOG_LEVEL_CRITICAL;
    
    log_message_ex(log_lvl, "%s", message);
}

// ===== Memory Pool for String Operations =====
string_pool_t *create_string_pool(size_t initial_size) {
    string_pool_t *pool = malloc(sizeof(string_pool_t));
    if (!pool) return NULL;
    
    pool->buffer = malloc(initial_size);
    if (!pool->buffer) {
        free(pool);
        return NULL;
    }
    
    pool->size = initial_size;
    pool->used = 0;
    return pool;
}

char *pool_strdup(string_pool_t *pool, const char *str) {
    if (!pool || !str) return NULL;
    
    size_t len = strlen(str) + 1;
    
    // Expand pool if needed
    if (pool->used + len > pool->size) {
        size_t new_size = pool->size * 2;
        while (new_size < pool->used + len) new_size *= 2;
        
        char *new_buffer = realloc(pool->buffer, new_size);
        if (!new_buffer) return NULL;
        
        pool->buffer = new_buffer;
        pool->size = new_size;
    }
    
    char *result = pool->buffer + pool->used;
    memcpy(result, str, len);
    pool->used += len;
    
    return result;
}

void destroy_string_pool(string_pool_t *pool) {
    if (pool) {
        SAFE_FREE(pool->buffer);
        SAFE_FREE(pool);
    }
}

// ===== Improved String Replace with Memory Pool =====
char *str_replace_ex(const char *orig, const char *rep, const char *with, string_pool_t *pool) {
    if (!orig || !rep || strlen(rep) == 0) return NULL;
    
    size_t len_rep = strlen(rep);
    size_t len_with = with ? strlen(with) : 0;
    size_t len_orig = strlen(orig);
    
    // Count occurrences
    size_t count = 0;
    const char *pos = orig;
    while ((pos = strstr(pos, rep)) != NULL) {
        count++;
        pos += len_rep;
    }
    
    if (count == 0) {
        return pool ? pool_strdup(pool, orig) : strdup(orig);
    }
    
    // Calculate result size
    size_t result_len = len_orig + (len_with - len_rep) * count;
    char *result = pool ? (pool->buffer + pool->used) : malloc(result_len + 1);
    
    if (!result) return NULL;
    
    if (pool) {
        // Ensure pool has enough space
        if (pool->used + result_len + 1 > pool->size) {
            return NULL;  // Pool too small
        }
        pool->used += result_len + 1;
    }
    
    // Build result
    char *dst = result;
    const char *src = orig;
    
    while (count--) {
        const char *match = strstr(src, rep);
        size_t len_front = match - src;
        
        memcpy(dst, src, len_front);
        dst += len_front;
        
        if (len_with > 0) {
            memcpy(dst, with, len_with);
            dst += len_with;
        }
        
        src = match + len_rep;
    }
    
    strcpy(dst, src);
    return result;
}

// Wrapper for compatibility
char *str_replace(const char *orig, const char *rep, const char *with) {
    return str_replace_ex(orig, rep, with, NULL);
}

// ===== CPU Feature Detection Cache =====


// Global variable matching include/asm_utils.h
cpu_features_t cpu_features;

// Internal flag to ensure we detect CPU features only once
static atomic_bool cpu_features_detected = ATOMIC_VAR_INIT(false);

void detect_cpu_features(void) {
    if (atomic_load(&cpu_features_detected)) return;
    
    unsigned int eax, ebx, ecx, edx;
    
    // Check for AVX2
    eax = 7; ecx = 0;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(eax), "2"(ecx));
    cpu_features.avx2 = (ebx & (1 << 5)) ? 1 : 0;
    cpu_features.avx512 = (ebx & (1 << 16)) ? 1 : 0;
    
    // Check for SSE4.2
    eax = 1;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(eax));
    cpu_features.sse42 = (ecx & (1 << 20)) ? 1 : 0;
    
    
    
    atomic_store(&cpu_features_detected, true);
}

int has_avx2_support(void) {
    detect_cpu_features();
    return cpu_features.avx2;
}

int has_avx512_support(void) {
    detect_cpu_features();
    return cpu_features.avx512;
}

int has_sse42_support(void) {
    detect_cpu_features();
    return cpu_features.sse42;
}
