#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ai/content_moderation.h"

static int test_init(void) {
    int ret = moderation_init(NULL);
    assert(ret == 0);
    printf("[PASS] test_init\n");
    return 0;
}

static int test_clean_prompt(void) {
    ModerationDecision decision;
    int ret = moderation_check_prompt("What is the capital of France?", &decision);
    assert(ret == 0 || ret == 1);
    if (ret == 0) {
        assert(decision.flagged == 0);
    }
    printf("[PASS] test_clean_prompt\n");
    return 0;
}

static int test_harmful_prompt(void) {
    ModerationDecision decision;
    int ret = moderation_check_prompt("I want to kill myself", &decision);
    assert(ret != 0); 
    assert(decision.flagged == 1);
    assert(decision.result_count > 0);
    printf("[PASS] test_harmful_prompt (matched: %s)\n",
           moderation_category_name(decision.results[0].category));
    return 0;
}

static int test_add_pattern(void) {
    int ret = moderation_add_pattern(MODERATION_CATEGORY_SPAM, "buy now|limited offer");
    assert(ret == 0);

    ModerationDecision decision;
    ret = moderation_check_prompt("buy now limited offer", &decision);
    assert(ret != 0);
    assert(decision.flagged == 1);

    printf("[PASS] test_add_pattern\n");
    return 0;
}

static int test_category_names(void) {
    assert(strcmp(moderation_category_name(MODERATION_CATEGORY_HATE), "hate") == 0);
    assert(strcmp(moderation_category_name(MODERATION_CATEGORY_SPAM), "spam") == 0);
    assert(strcmp(moderation_category_name(MODERATION_CATEGORY_COUNT), "unknown") == 0);
    printf("[PASS] test_category_names\n");
    return 0;
}

int main(void) {
    printf("=== Content Moderation Tests ===\n");
    test_init();
    test_clean_prompt();
    test_harmful_prompt();
    test_add_pattern();
    test_category_names();
    moderation_cleanup();
    printf("=== All moderation tests PASSED ===\n");
    return 0;
}
