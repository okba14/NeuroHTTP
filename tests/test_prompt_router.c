#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "prompt_router.h"

static int test_json_escape(void) {
    extern char *json_escape(const char *in);
    char *r = json_escape("hello \"world\"\nline2");
    assert(r != NULL);
    assert(strcmp(r, "hello \\\"world\\\"\\nline2") == 0);
    free(r);

    r = json_escape("tab\there");
    assert(r != NULL);
    assert(strcmp(r, "tab\\there") == 0);
    free(r);

    r = json_escape("back\\slash");
    assert(strcmp(r, "back\\\\slash") == 0);
    free(r);

    r = json_escape(NULL);
    assert(r != NULL && strcmp(r, "") == 0);
    free(r);

    printf("[PASS] test_json_escape\n");
    return 0;
}

static int test_model_add_remove(void) {
    prompt_router_init();
    int ret = prompt_router_add_model("test-model", "https://api.test.com/v1", "TEST_KEY", 4096, 0.7, 2);
    assert(ret == 0);

    int count = prompt_router_get_available_count();
    assert(count >= 0);

    ret = prompt_router_remove_model("test-model");
    assert(ret == 0);

    prompt_router_cleanup();
    printf("[PASS] test_model_add_remove\n");
    return 0;
}

static int test_model_routing(void) {
    prompt_router_init();
    prompt_router_set_default_model("test-model");
    prompt_router_add_model("test-model", "https://api.test.com/v1/chat/completions", "TEST_KEY", 4096, 0.7, 2);

    char response[4096] = {0};
    char actual_model[128] = {0};
    int ret = prompt_router_route("hello world", "test-model", response, sizeof(response), actual_model, sizeof(actual_model));
    assert(ret == 0 || ret == -1);
    if (ret == 0) {
        assert(strlen(actual_model) > 0);
    }

    prompt_router_cleanup();
    printf("[PASS] test_model_routing\n");
    return 0;
}

static int test_cost_accounting(void) {
    prompt_router_init();
    prompt_router_add_model("cost-test", "https://api.test.com/v1", "TEST_KEY", 4096, 0.7, 1);

    TokenAccount acc;
    int ret = prompt_router_get_token_account(&acc);
    assert(ret == 0);
    assert(acc.total_requests == 0);

    prompt_router_cleanup();
    printf("[PASS] test_cost_accounting\n");
    return 0;
}

static int test_capability_flags(void) {
    CapabilityFlags caps = CAP_STREAMING | CAP_VISION | CAP_TOOL_USE;
    assert(caps & CAP_STREAMING);
    assert(caps & CAP_VISION);
    assert(caps & CAP_TOOL_USE);
    assert(!(caps & CAP_REASONING));

    printf("[PASS] test_capability_flags\n");
    return 0;
}

int main(void) {
    printf("=== Prompt Router Tests ===\n");
    test_json_escape();
    test_model_add_remove();
    test_model_routing();
    test_cost_accounting();
    test_capability_flags();
    printf("=== All prompt router tests PASSED ===\n");
    return 0;
}
