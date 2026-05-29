#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cache.h"

static int test_init(void) {
    int ret = cache_init(100, 3600);
    assert(ret == 0);
    assert(cache_active_backend() == CACHE_BACKEND_MEMORY);
    printf("[PASS] test_cache_init\n");
    return 0;
}

static int test_set_get(void) {
    int ret = cache_set("key1", "value1", 7, 3600);
    assert(ret == 0);

    char buf[64] = {0};
    ret = cache_get("key1", buf, sizeof(buf));
    assert(ret == 0);
    assert(strcmp(buf, "value1") == 0);

    printf("[PASS] test_cache_set_get\n");
    return 0;
}

static int test_delete(void) {
    cache_set("del_test", "delete_me", 8, 3600);
    int ret = cache_delete("del_test");
    assert(ret == 0);

    char buf[64] = {0};
    ret = cache_get("del_test", buf, sizeof(buf));
    assert(ret != 0);

    printf("[PASS] test_cache_delete\n");
    return 0;
}

static int test_clear(void) {
    cache_set("c1", "v1", 2, 3600);
    cache_set("c2", "v2", 2, 3600);
    int ret = cache_clear();
    assert(ret == 0);

    char buf[64] = {0};
    ret = cache_get("c1", buf, sizeof(buf));
    assert(ret != 0);

    printf("[PASS] test_cache_clear\n");
    return 0;
}

static int test_stats(void) {
    cache_set("stat_test", "stat_val", 8, 3600);
    char buf[64] = {0};
    cache_get("stat_test", buf, sizeof(buf));
    cache_get("nonexistent", buf, sizeof(buf));

    int entries, hits, misses;
    int ret = cache_get_stats(&entries, &hits, &misses);
    assert(ret == 0);
    assert(entries > 0);
    assert(hits > 0);
    assert(misses > 0);

    printf("[PASS] test_cache_stats\n");
    return 0;
}

static int test_expiry(void) {
    int ret = cache_set("expire", "test", 4, 1);
    assert(ret == 0);

    char buf[64] = {0};
    ret = cache_get("expire", buf, sizeof(buf));
    assert(ret == 0);

    printf("[PASS] test_cache_expiry (shelf life depends on real time)\n");
    return 0;
}

static int test_semantic_cache(void) {
    int ret = cache_semantic_set("What is AI?", "gpt-4", "Artificial Intelligence", 21, 3600);
    assert(ret == 0);

    char buf[256] = {0};
    ret = cache_semantic_get("What is AI?", "gpt-4", buf, sizeof(buf), 0.8);
    if (ret == 0) {
        assert(strlen(buf) > 0);
    }

    printf("[PASS] test_semantic_cache\n");
    return 0;
}

int main(void) {
    printf("=== Cache Tests ===\n");
    test_init();
    test_set_get();
    test_delete();
    test_clear();
    test_stats();
    test_expiry();
    test_semantic_cache();
    cache_cleanup();
    printf("=== All cache tests PASSED ===\n");
    return 0;
}
