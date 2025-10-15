#ifndef AIONIC_AI_TOKENIZER_H
#define AIONIC_AI_TOKENIZER_H

#include <stddef.h>

typedef struct {
    char *text;
    int id;
    int type;  // 0: word, 1: punctuation, 2: number, 3: special
} Token;


int tokenizer_init();
int tokenizer_tokenize(const char *text, Token ***tokens, int *token_count);
int tokenizer_detokenize(Token **tokens, int token_count, char *output, size_t output_size);
void tokenizer_free_tokens(Token **tokens, int token_count);
void tokenizer_free_token(Token *token);
const char *tokenizer_get_type_name(int type);
void tokenizer_cleanup();

#endif // AIONIC_AI_TOKENIZER_H
