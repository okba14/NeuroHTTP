#ifndef AIONIC_UTILS_H
#define AIONIC_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <stdbool.h>

// ===== Original Functions =====
char *read_file(const char *filename);
uint64_t get_current_time_ms(void);
void generate_random_string(char *buffer, size_t length);
int string_to_int(const char *str, int *result);
void log_message(const char *level, const char *message);
void print_hex(const char *data, size_t length);
int mkdir_if_not_exists(const char *path);
char *get_current_time_str(void);
char *url_encode(const char *str);
char *url_decode(const char *str);
int is_file_exists(const char *filename);
int create_directory(const char *path);
char *get_executable_path(void);

char *str_replace(const char *orig, const char *rep, const char *with);
char **str_split(const char *str, const char *delim, int *count);
char *str_trim(char *str);
int str_starts_with(const char *str, const char *prefix);
int str_ends_with(const char *str, const char *suffix);

int is_port_available(int port);
char *get_ip_address(void);
int resolve_hostname(const char *hostname, char *ip_buffer, size_t ip_buffer_size);

char *md5_string(const char *str);
char *sha256_string(const char *str);
int base64_encode(const unsigned char *input, int length, char *output);
int base64_decode(const char *input, unsigned char *output, int *out_length);

typedef struct {
    void **items;
    size_t size;
    size_t capacity;
} Array;

Array *array_create(size_t initial_capacity);
void array_free(Array *array);
int array_append(Array *array, void *item);
void *array_get(Array *array, size_t index);
size_t array_size(Array *array);

typedef struct {
    char *key;
    char *value;
} KeyValuePair;

typedef struct {
    KeyValuePair *items;
    size_t size;
    size_t capacity;
} Dictionary;

Dictionary *dictionary_create(size_t initial_capacity);
void dictionary_free(Dictionary *dict);
int dictionary_set(Dictionary *dict, const char *key, const char *value);
char *dictionary_get(Dictionary *dict, const char *key);
size_t dictionary_size(Dictionary *dict);

// ===== New Functions =====
char *read_file_ex(const char *filename, size_t *out_size, int *error_code);
uint64_t get_current_time_ns(void);
uint64_t get_current_time_us(void);
void generate_random_string_safe(char *buffer, size_t length);
int string_to_int_ex(const char *str, int *result, int base);

// ===== Logging System =====
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_CRITICAL = 4
} log_level_t;

void init_logging(const char *log_filename, log_level_t level);
void log_message_ex(log_level_t level, const char *format, ...);

// ===== Memory Pool =====
typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} string_pool_t;

string_pool_t *create_string_pool(size_t initial_size);
char *pool_strdup(string_pool_t *pool, const char *str);
void destroy_string_pool(string_pool_t *pool);
char *str_replace_ex(const char *orig, const char *rep, const char *with, string_pool_t *pool);

// ===== CPU Features =====
int has_avx2_support(void);
int has_avx512_support(void);

#endif // AIONIC_UTILS_H
