#ifndef ASM_UTILS_H
#define ASM_UTILS_H

#include <stddef.h>
#include <stdint.h>

// CRC32 functions
extern uint32_t crc32_asm(const void *data, size_t length);
extern uint32_t crc32_asm_avx2(const void *data, size_t length);

// JSON tokenizer functions
extern void json_fast_tokenizer(const char *json_str, size_t length);
extern void json_fast_tokenizer_avx2(const char *json_str, size_t length);

// Memory copy functions
extern void *memcpy_asm(void *dest, const void *src, size_t n);
extern void *memcpy_asm_avx2(void *dest, const void *src, size_t n);
extern void *memcpy_asm_avx512(void *dest, const void *src, size_t n);

// Hardware detection functions
int has_avx2_support(void);
int has_avx512_support(void);

#endif // ASM_UTILS_H
