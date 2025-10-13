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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "tokenizer.h"
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <string.h>
#include <stdlib.h>

// تعريف بنئة الرمز المميز
typedef struct {
    char *text;
    int id;
    int type;  // 0: word, 1: punctuation, 2: number, 3: special
} Token;

// تعريف بنئة المميز
typedef struct {
    Token *tokens;
    int token_count;
    int token_capacity;
    char *vocabulary;
    int vocabulary_size;
} Tokenizer;

static Tokenizer global_tokenizer;

// دالة للتحقق مما إذا كان الحرف هو علامة ترقيم
static int is_punctuation(char c) {
    return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || 
           c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || 
           c == '\"' || c == '\'' || c == '-' || c == '_';
}

// دالة للتحقق مما إذا كان الحرف هو رقم
static int is_number_char(char c) {
    return isdigit(c) || c == '.';
}

// دالة لتقسيم النص إلى كلمات
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
        // تخطي المسافات البيضاء
        while (*ptr && isspace(*ptr)) {
            ptr++;
        }
        
        if (!*ptr) {
            break;
        }
        
        // تحديد نوع الرمز
        int type = 0;  // word
        if (is_punctuation(*ptr)) {
            type = 1;  // punctuation
        } else if (is_number_char(*ptr)) {
            type = 2;  // number
        }
        
        // استخراج الرمز
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
        
        // إنشاء رمز جديد
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
            // تحرير الذاكرة المخصصة سابقاً
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

// تهيئة المميز
int tokenizer_init() {
    global_tokenizer.token_capacity = 1024;
    global_tokenizer.tokens = malloc(sizeof(Token) * global_tokenizer.token_capacity);
    if (!global_tokenizer.tokens) {
        return -1;
    }
    
    global_tokenizer.token_count = 0;
    global_tokenizer.vocabulary = NULL;
    global_tokenizer.vocabulary_size = 0;
    
    log_message("TOKENIZER", "Tokenizer initialized");
    return 0;
}

// تمييز نص
int tokenizer_tokenize(const char *text, Token ***tokens, int *token_count) {
    if (!text || !tokens || !token_count) {
        return -1;
    }
    
    // تقسيم النص إلى رموز
    Token *raw_tokens;
    int raw_token_count;
    
    if (tokenize_text(text, &raw_tokens, &raw_token_count) != 0) {
        return -1;
    }
    
    // نسخ الرموز إلى المخرجات
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
            // تحرير الذاكرة المخصصة سابقاً
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
    
    // تحرير الذاكرة المؤقتة
    for (int i = 0; i < raw_token_count; i++) {
        free(raw_tokens[i].text);
    }
    free(raw_tokens);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Tokenized text into %d tokens", *token_count);
    log_message("TOKENIZER", log_msg);
    
    return 0;
}

// تحويل الرموز إلى نص
int tokenizer_detokenize(Token **tokens, int token_count, char *output, size_t output_size) {
    if (!tokens || token_count == 0 || !output || output_size == 0) {
        return -1;
    }
    
    size_t total_length = 0;
    
    // حساب الطول الإجمالي المطلوب
    for (int i = 0; i < token_count; i++) {
        if (tokens[i]) {
            total_length += strlen(tokens[i]->text);
            
            // إضافة مسافة إذا لزم الأمر
            if (i < token_count - 1 && tokens[i]->type == 0 && tokens[i+1]->type == 0) {
                total_length++;
            }
        }
    }
    
    if (total_length >= output_size) {
        return -1;
    }
    
    // بناء النص
    output[0] = '\0';
    for (int i = 0; i < token_count; i++) {
        if (tokens[i]) {
            strcat(output, tokens[i]->text);
            
            // إضافة مسافة إذا لزم الأمر
            if (i < token_count - 1 && tokens[i]->type == 0 && tokens[i+1]->type == 0) {
                strcat(output, " ");
            }
        }
    }
    
    return 0;
}

// تحرير ذاكرة الرموز
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

// تحرير ذاكرة الرمز الواحد
void tokenizer_free_token(Token *token) {
    if (!token) {
        return;
    }
    
    if (token->text) {
        free(token->text);
    }
    
    free(token);
}

// الحصول على نوع الرمز كنص
const char *tokenizer_get_type_name(int type) {
    switch (type) {
        case 0: return "word";
        case 1: return "punctuation";
        case 2: return "number";
        case 3: return "special";
        default: return "unknown";
    }
}

// تنظيف المميز
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
    
    log_message("TOKENIZER", "Tokenizer cleaned up");
}
