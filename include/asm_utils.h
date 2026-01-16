#ifndef ASM_UTILS_H
#define ASM_UTILS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 *  CPU Feature Detection
 * ============================================================ */

typedef struct {
    uint8_t sse42;
    uint8_t avx2;
    uint8_t avx512;
} cpu_features_t;

/* Global CPU feature flags (initialized at startup) */
extern cpu_features_t cpu_features;

/* Detect CPU + OS supported features */
void detect_cpu_features(void);

/* ============================================================
 *  CRC32 Functions
 * ============================================================ */

/* Scalar / SSE4.2 */
uint32_t crc32_asm(const void *data, size_t length);

/* AVX2 accelerated */
uint32_t crc32_asm_avx2(const void *data, size_t length);

/* ============================================================
 *  JSON Tokenizer
 * ============================================================ */

void json_fast_tokenizer(const char *json_str, size_t length);
void json_fast_tokenizer_avx2(const char *json_str, size_t length);

/* ============================================================
 *  Memory Copy
 * ============================================================ */

void *memcpy_asm(void *dest, const void *src, size_t n);
void *memcpy_asm_avx2(void *dest, const void *src, size_t n);
void *memcpy_asm_avx512(void *dest, const void *src, size_t n);

/* ============================================================
 *  Runtime Capability Queries
 * ============================================================ */

int has_sse42_support(void);
int has_avx2_support(void);
int has_avx512_support(void);

#ifdef __cplusplus
}
#endif

#endif /* ASM_UTILS_H */
