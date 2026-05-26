#ifndef AIONIC_ARENA_H
#define AIONIC_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct Arena Arena;

Arena *arena_create(size_t capacity);
void *arena_alloc(Arena *a, size_t size);
void *arena_alloc_aligned(Arena *a, size_t size, size_t alignment);
void arena_reset(Arena *a);
void arena_destroy(Arena *a);
size_t arena_used(Arena *a);
size_t arena_remaining(Arena *a);

typedef struct Slab Slab;
Slab *slab_create(size_t object_size, size_t object_count);
void *slab_alloc(Slab *s);
void slab_free(Slab *s, void *obj);
void slab_destroy(Slab *s);

typedef struct ArenaPool ArenaPool;
ArenaPool *arenapool_create(size_t arena_size, int thread_count);
Arena *arenapool_get(ArenaPool *pool);
void arenapool_reset_all(ArenaPool *pool);
void arenapool_destroy(ArenaPool *pool);

#endif
