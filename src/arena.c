#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include "arena.h"

struct Arena {
    char *buf;
    size_t capacity;
    size_t offset;
    int owns_buffer;
};

Arena *arena_create(size_t capacity) {
    Arena *a = malloc(sizeof(Arena));
    if (!a) return NULL;
    a->buf = malloc(capacity);
    if (!a->buf) { free(a); return NULL; }
    a->capacity = capacity;
    a->offset = 0;
    a->owns_buffer = 1;
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    size_t aligned = (size + 7) & ~7ULL;
    if (a->offset + aligned > a->capacity) return NULL;
    void *ptr = a->buf + a->offset;
    a->offset += aligned;
    return ptr;
}

void *arena_alloc_aligned(Arena *a, size_t size, size_t alignment) {
    size_t mask = alignment - 1;
    size_t current = (size_t)(a->buf + a->offset);
    size_t aligned_offset = (current + mask) & ~mask;
    size_t aligned_pos = aligned_offset - (size_t)a->buf;
    if (aligned_pos + size > a->capacity) return NULL;
    a->offset = aligned_pos + size;
    return a->buf + aligned_pos;
}

void arena_reset(Arena *a) {
    a->offset = 0;
}

void arena_destroy(Arena *a) {
    if (a) {
        if (a->owns_buffer && a->buf) free(a->buf);
        free(a);
    }
}

size_t arena_used(Arena *a) { return a->offset; }
size_t arena_remaining(Arena *a) { return a->capacity - a->offset; }

struct Slab {
    size_t object_size;
    size_t object_count;
    size_t free_count;
    void *memory;
    int *free_list;
};

Slab *slab_create(size_t object_size, size_t object_count) {
    Slab *s = malloc(sizeof(Slab));
    if (!s) return NULL;
    s->object_size = (object_size + 63) & ~63ULL;
    s->object_count = object_count;
    s->free_count = object_count;
    s->memory = calloc(object_count, s->object_size);
    if (!s->memory) { free(s); return NULL; }
    s->free_list = malloc(sizeof(int) * object_count);
    if (!s->free_list) { free(s->memory); free(s); return NULL; }
    for (size_t i = 0; i < object_count; i++) s->free_list[i] = (int)i;
    return s;
}

void *slab_alloc(Slab *s) {
    if (s->free_count == 0) return NULL;
    s->free_count--;
    int idx = s->free_list[s->free_count];
    return (char*)s->memory + idx * s->object_size;
}

void slab_free(Slab *s, void *obj) {
    if (s->free_count >= s->object_count) return;
    ptrdiff_t offset = (char*)obj - (char*)s->memory;
    int idx = (int)(offset / (ptrdiff_t)s->object_size);
    s->free_list[s->free_count] = idx;
    s->free_count++;
}

void slab_destroy(Slab *s) {
    if (s) {
        free(s->memory);
        free(s->free_list);
        free(s);
    }
}

struct ArenaPool {
    Arena **arenas;
    int count;
    size_t arena_size;
};

ArenaPool *arenapool_create(size_t arena_size, int thread_count) {
    ArenaPool *pool = malloc(sizeof(ArenaPool));
    if (!pool) return NULL;
    pool->count = thread_count > 0 ? thread_count : 1;
    pool->arena_size = arena_size;
    pool->arenas = calloc(pool->count, sizeof(Arena*));
    if (!pool->arenas) { free(pool); return NULL; }
    for (int i = 0; i < pool->count; i++) {
        pool->arenas[i] = arena_create(arena_size);
        if (!pool->arenas[i]) {
            for (int j = 0; j < i; j++) arena_destroy(pool->arenas[j]);
            free(pool->arenas); free(pool); return NULL;
        }
    }
    return pool;
}

Arena *arenapool_get(ArenaPool *pool) {
    size_t self = (size_t)pthread_self();
    int idx = (int)(self % (size_t)pool->count);
    return pool->arenas[idx];
}

void arenapool_reset_all(ArenaPool *pool) {
    for (int i = 0; i < pool->count; i++) arena_reset(pool->arenas[i]);
}

void arenapool_destroy(ArenaPool *pool) {
    if (pool) {
        for (int i = 0; i < pool->count; i++) arena_destroy(pool->arenas[i]);
        free(pool->arenas);
        free(pool);
    }
}
