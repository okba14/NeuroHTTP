#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "tokenizer.h"
#include "utils.h"
#include "asm_utils.h"

// Tokenizer structure definition
typedef struct {
    Token *tokens;
    int token_count;
    int token_capacity;
    char *vocabulary;
    int vocabulary_size;
} Tokenizer;

static Tokenizer global_tokenizer;

// Function to check if a character is punctuation
static int is_punctuation(char c) {
    return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || 
           c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || 
           c == '\"' || c == '\'' || c == '-' || c == '_';
}

// Function to check if a character is part of a number
static int is_number_char(char c) {
    return isdigit(c) || c == '.';
}

// Function to split text into tokens
static int tokenize_text(const char *text, Token **tokens, int *token_count) {
    if (!text || !tokens || !token_count) {
        return -1;
    }
    
    int capacity = 64;
    *tokens = malloc(sizeof(Token) * capacity);
    *token_count = 0;
    
    if (!*tokens) {
        return -1;
    }
    
    const char *ptr = text;
    while (*ptr) {
        // Skip whitespace
        while (*ptr && isspace(*ptr)) {
            ptr++;
        }
        
        if (!*ptr) {
            break;
        }
        
        // Determine token type
        int type = 0;  // word
        if (is_punctuation(*ptr)) {
            type = 1;  // punctuation
        } else if (is_number_char(*ptr)) {
            type = 2;  // number
        }
        
        // Extract token
        const char *start = ptr;
        while (*ptr) {
            if (type == 0 && (isspace(*ptr) || is_punctuation(*ptr))) {
                break;
            } else if (type == 1 && !is_punctuation(*ptr)) {
                break;
            } else if (type == 2 && !is_number_char(*ptr)) {
                break;
            }
            ptr++;
        }
        
        // Create new token
        if (*token_count >= capacity) {
            capacity *= 2;
            Token *new_tokens = realloc(*tokens, sizeof(Token) * capacity);
            if (!new_tokens) {
                free(*tokens);
                return -1;
            }
            *tokens = new_tokens;
        }
        
        int length = ptr - start;
        (*tokens)[*token_count].text = malloc(length + 1);
        if (!(*tokens)[*token_count].text) {
            // Free previously allocated memory
            for (int i = 0; i < *token_count; i++) {
                free((*tokens)[i].text);
            }
            free(*tokens);
            return -1;
        }
        
        strncpy((*tokens)[*token_count].text, start, length);
        (*tokens)[*token_count].text[length] = '\0';
        (*tokens)[*token_count].id = *token_count;
        (*tokens)[*token_count].type = type;
        
        (*token_count)++;
    }
    
    return 0;
}

// Function to tokenize text using optimized functions
int tokenize_text_optimized(const char *text, size_t length, Token *tokens, size_t max_tokens) {
    // Use optimized memcpy for copying text
    char *text_copy = malloc(length + 1);
    if (!text_copy) {
        return -1;
    }
    
    memcpy_asm(text_copy, text, length);
    text_copy[length] = '\0';
    
    // Simple tokenization - in a real implementation, this would be more complex
    char *token = strtok(text_copy, " \t\n\r");
    size_t token_count = 0;
    
    while (token && token_count < max_tokens) {
        tokens[token_count].text = strdup(token);
        // Note: We don't set length field here as it's not part of Token structure
        token_count++;
        
        token = strtok(NULL, " \t\n\r");
    }
    
    free(text_copy);
    return token_count;
}

// Initialize tokenizer
int tokenizer_init() {
    global_tokenizer.token_capacity = 1024;
    global_tokenizer.tokens = malloc(sizeof(Token) * global_tokenizer.token_capacity);
    if (!global_tokenizer.tokens) {
        return -1;
    }
    
    global_tokenizer.token_count = 0;
    global_tokenizer.vocabulary = NULL;
    global_tokenizer.vocabulary_size = 0;
    
    // Use log_message if available, otherwise use printf
    #ifdef LOG_MESSAGE_AVAILABLE
    log_message("TOKENIZER", "Tokenizer initialized");
    #else
    printf("[TOKENIZER] Tokenizer initialized\n");
    #endif
    
    return 0;
}

// Tokenize text
int tokenizer_tokenize(const char *text, Token ***tokens, int *token_count) {
    if (!text || !tokens || !token_count) {
        return -1;
    }
    
    // Split text into tokens
    Token *raw_tokens;
    int raw_token_count;
    
    if (tokenize_text(text, &raw_tokens, &raw_token_count) != 0) {
        return -1;
    }
    
    // Copy tokens to output
    *tokens = malloc(sizeof(Token *) * raw_token_count);
    if (!*tokens) {
        for (int i = 0; i < raw_token_count; i++) {
            free(raw_tokens[i].text);
        }
        free(raw_tokens);
        return -1;
    }
    
    for (int i = 0; i < raw_token_count; i++) {
        (*tokens)[i] = malloc(sizeof(Token));
        if (!(*tokens)[i]) {
            // Free previously allocated memory
            for (int j = 0; j < i; j++) {
                free((*tokens)[j]);
            }
            free(*tokens);
            for (int j = 0; j < raw_token_count; j++) {
                free(raw_tokens[j].text);
            }
            free(raw_tokens);
            return -1;
        }
        
        (*tokens)[i]->text = strdup(raw_tokens[i].text);
        (*tokens)[i]->id = raw_tokens[i].id;
        (*tokens)[i]->type = raw_tokens[i].type;
    }
    
    *token_count = raw_token_count;
    
    // Free temporary memory
    for (int i = 0; i < raw_token_count; i++) {
        free(raw_tokens[i].text);
    }
    free(raw_tokens);
    
    // Use log_message if available, otherwise use printf
    #ifdef LOG_MESSAGE_AVAILABLE
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Tokenized text into %d tokens", *token_count);
    log_message("TOKENIZER", log_msg);
    #else
    printf("[TOKENIZER] Tokenized text into %d tokens\n", *token_count);
    #endif
    
    return 0;
}

// Convert tokens back to text
int tokenizer_detokenize(Token **tokens, int token_count, char *output, size_t output_size) {
    if (!tokens || token_count == 0 || !output || output_size == 0) {
        return -1;
    }
    
    size_t total_length = 0;
    
    // Calculate total required length
    for (int i = 0; i < token_count; i++) {
        if (tokens[i]) {
            total_length += strlen(tokens[i]->text);
            
            // Add space if needed
            if (i < token_count - 1 && tokens[i]->type == 0 && tokens[i+1]->type == 0) {
                total_length++;
            }
        }
    }
    
    if (total_length >= output_size) {
        return -1;
    }
    
    // Build text
    output[0] = '\0';
    for (int i = 0; i < token_count; i++) {
        if (tokens[i]) {
            strcat(output, tokens[i]->text);
            
            // Add space if needed
            if (i < token_count - 1 && tokens[i]->type == 0 && tokens[i+1]->type == 0) {
                strcat(output, " ");
            }
        }
    }
    
    return 0;
}

// Free tokens memory
void tokenizer_free_tokens(Token **tokens, int token_count) {
    if (!tokens || token_count == 0) {
        return;
    }
    
    for (int i = 0; i < token_count; i++) {
        if (tokens[i]) {
            if (tokens[i]->text) {
                free(tokens[i]->text);
            }
            free(tokens[i]);
        }
    }
    
    free(tokens);
}

// Free single token memory
void tokenizer_free_token(Token *token) {
    if (!token) {
        return;
    }
    
    if (token->text) {
        free(token->text);
    }
    
    free(token);
}

// Get token type as string
const char *tokenizer_get_type_name(int type) {
    switch (type) {
        case 0: return "word";
        case 1: return "punctuation";
        case 2: return "number";
        case 3: return "special";
        default: return "unknown";
    }
}

// Clean up tokenizer
void tokenizer_cleanup() {
    for (int i = 0; i < global_tokenizer.token_count; i++) {
        if (global_tokenizer.tokens[i].text) {
            free(global_tokenizer.tokens[i].text);
        }
    }
    
    free(global_tokenizer.tokens);
    if (global_tokenizer.vocabulary) {
        free(global_tokenizer.vocabulary);
    }
    
    // Use log_message if available, otherwise use printf
    #ifdef LOG_MESSAGE_AVAILABLE
    log_message("TOKENIZER", "Tokenizer cleaned up");
    #else
    printf("[TOKENIZER] Tokenizer cleaned up\n");
    #endif
}
