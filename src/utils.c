#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>    // ← مهم لـ isspace
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include "utils.h"

char *read_file(const char *filename) {
    if (!filename) return NULL;
    
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    fread(content, 1, file_size, file);
    content[file_size] = '\0';
    
    fclose(file);
    return content;
}

uint64_t get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void generate_random_string(char *buffer, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    if (!buffer || length == 0) return;
    
    srand(time(NULL));
    
    for (size_t i = 0; i < length - 1; i++) {
        int key = rand() % (sizeof(charset) - 1);
        buffer[i] = charset[key];
    }
    
    buffer[length - 1] = '\0';
}

int string_to_int(const char *str, int *result) {
    if (!str || !result) return -1;
    
    char *endptr;
    *result = (int)strtol(str, &endptr, 10);
    
    if (*endptr != '\0') {
        return -1;  // تحويل غير صالح
    }
    
    return 0;
}

void log_message(const char *level, const char *message) {
    time_t now;
    time(&now);
    
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] [%s] %s\n", time_str, level, message);
}

void print_hex(const char *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", (unsigned char)data[i]);
        
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    
    if (length % 16 != 0) {
        printf("\n");
    }
}

int mkdir_if_not_exists(const char *path) {
    struct stat st = {0};
    
    if (stat(path, &st) == -1) {
        return mkdir(path, 0700);
    }
    
    return 0;
}

char *get_current_time_str() {
    time_t now;
    time(&now);
    
    char *time_str = malloc(64);
    if (!time_str) return NULL;
    
    strftime(time_str, 64, "%Y-%m-%d %H:%M:%S", localtime(&now));
    return time_str;
}

char *str_replace(const char *orig, const char *rep, const char *with) {
    char *result;
    char *ins;
    char *tmp;
    int len_rep = 0;
    int len_with = 0;
    int len_front = 0;
    int count = 0;  // ← إصلاح: تعريف المتغير count هنا
    
    if (!orig || !rep) return NULL;
    
    len_rep = strlen(rep);
    if (len_rep == 0) return NULL; // Nothing to replace.
    
    if (!with) with = "";
    len_with = strlen(with);
    
    ins = (char *)orig;
    while ((tmp = strstr(ins, rep))) {  // ← إصلاح: حساب عدد التكرارات
        count++;
        ins = tmp + len_rep;
    }
    
    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);
    
    if (!result) return NULL;
    
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep;
    }
    
    strcpy(tmp, orig);
    return result;
}

char **str_split(const char *str, const char *delim, int *count) {
    char *copy = strdup(str);
    if (!copy) return NULL;
    
    char *token;
    char **result = NULL;
    int i = 0;
    
    token = strtok(copy, delim);
    while (token) {
        result = realloc(result, sizeof(char *) * (i + 1));
        if (!result) {
            free(copy);
            return NULL;
        }
        
        result[i] = strdup(token);
        if (!result[i]) {
            for (int j = 0; j < i; j++) free(result[j]);
            free(result);
            free(copy);
            return NULL;
        }
        
        i++;
        token = strtok(NULL, delim);
    }
    
    *count = i;
    free(copy);
    return result;
}

char *str_trim(char *str) {
    if (!str) return NULL;
    
    char *end;
    
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == '\0') return str;
    
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    *(end + 1) = '\0';
    
    return str;
}

int str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

int str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return 0;
    
    return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}
