#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/parser.h"

int test_json_parsing() {
    printf("Testing JSON parsing...\n");
    
    const char *json_string = "{\"prompt\": \"Hello, world!\", \"model\": \"test\"}";
    char prompt[1024] = {0};
    
    if (parse_json(json_string, prompt) != 0) {
        printf("FAILED: JSON parsing\n");
        return -1;
    }
    
    if (strcmp(prompt, "Hello, world!") != 0) {
        printf("FAILED: Incorrect prompt value\n");
        return -1;
    }
    
    printf("PASSED: JSON parsing\n");
    return 0;
}

int test_json_get_value() {
    printf("Testing JSON value extraction...\n");
    
    const char *json_string = "{\"name\": \"John\", \"age\": 30, \"active\": true}";
    char value[1024] = {0};
    
    if (json_get_value(json_string, "name", value, sizeof(value)) != 0) {
        printf("FAILED: JSON get value\n");
        return -1;
    }
    
    if (strcmp(value, "John") != 0) {
        printf("FAILED: Incorrect name value\n");
        return -1;
    }
    
    printf("PASSED: JSON value extraction\n");
    return 0;
}

int main() {
    printf("Running JSON tests...\n");
    
    if (test_json_parsing() != 0) {
        printf("JSON tests FAILED\n");
        return -1;
    }
    
    if (test_json_get_value() != 0) {
        printf("JSON tests FAILED\n");
        return -1;
    }
    
    printf("All JSON tests PASSED\n");
    return 0;
}
