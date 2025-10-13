#ifndef AIONIC_UTILS_H
#define AIONIC_UTILS_H

#include <stdint.h>
#include <stddef.h>


char *read_file(const char *filename);
uint64_t get_current_time_ms();
void generate_random_string(char *buffer, size_t length);
int string_to_int(const char *str, int *result);
void log_message(const char *level, const char *message);
void print_hex(const char *data, size_t length);

#endif // AIONIC_UTILS_H
