#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <regex.h>
#include <ctype.h>
#include "ai/content_moderation.h"
#include "utils.h"

typedef struct {
    ModerationCategory category;
    regex_t regex;
    char *pattern_str;
    int compiled;
} ModerationRule;

typedef struct {
    ModerationRule *rules;
    int rule_count;
    int rule_capacity;
    pthread_mutex_t mutex;
    int initialized;
} ModerationEngine;

static ModerationEngine engine;

static const char *default_patterns[] = {
    "hate speech|racial slurs?|white power|nazi",
    "harass|bully|doxx|kill yourself",
    "explicit sexual|nsfw|onlyfans|porn",
    "bomb|terrorist|mass shooting|kill (?:everyone|people|them)",
    "suicide|self-harm|kill myself|cut myself|end my life",
    "buy followers|click here|crypto giveaway|free money",
    "(?:exec|system|shell_exec|passthru|eval)\\s*\\(",
    NULL
};

static ModerationCategory default_categories[] = {
    MODERATION_CATEGORY_HATE,
    MODERATION_CATEGORY_HARASSMENT,
    MODERATION_CATEGORY_SEXUAL,
    MODERATION_CATEGORY_VIOLENCE,
    MODERATION_CATEGORY_SELF_HARM,
    MODERATION_CATEGORY_SPAM,
    MODERATION_CATEGORY_MALICIOUS_CODE
};

int moderation_init(const char *custom_patterns_file) {
    (void)custom_patterns_file;
    pthread_mutex_init(&engine.mutex, NULL);
    engine.rule_capacity = 64;
    engine.rules = calloc(engine.rule_capacity, sizeof(ModerationRule));
    if (!engine.rules) return -1;

    for (int i = 0; default_patterns[i]; i++) {
        if (moderation_add_pattern(default_categories[i], default_patterns[i]) != 0) continue;
    }

    engine.initialized = 1;
    log_message("MODERATION", "Content moderation initialized");
    return 0;
}

int moderation_add_pattern(ModerationCategory category, const char *pattern) {
    if (!pattern) return -1;
    pthread_mutex_lock(&engine.mutex);

    if (engine.rule_count >= engine.rule_capacity) {
        int new_cap = engine.rule_capacity * 2;
        ModerationRule *new_rules = realloc(engine.rules, sizeof(ModerationRule) * new_cap);
        if (!new_rules) { pthread_mutex_unlock(&engine.mutex); return -1; }
        memset(new_rules + engine.rule_capacity, 0, sizeof(ModerationRule) * (new_cap - engine.rule_capacity));
        engine.rules = new_rules;
        engine.rule_capacity = new_cap;
    }

    ModerationRule *rule = &engine.rules[engine.rule_count];
    rule->category = category;
    rule->pattern_str = strdup(pattern);
    if (!rule->pattern_str) { pthread_mutex_unlock(&engine.mutex); return -1; }

    int ret = regcomp(&rule->regex, pattern, REG_EXTENDED | REG_ICASE | REG_NOSUB);
    if (ret != 0) {
        free(rule->pattern_str);
        pthread_mutex_unlock(&engine.mutex);
        return -1;
    }
    rule->compiled = 1;
    engine.rule_count++;
    pthread_mutex_unlock(&engine.mutex);
    return 0;
}

static int moderation_check(const char *text, ModerationDecision *decision) {
    if (!text || !decision || !engine.initialized) return -1;
    memset(decision, 0, sizeof(ModerationDecision));

    pthread_mutex_lock(&engine.mutex);
    for (int i = 0; i < engine.rule_count && decision->result_count < 16; i++) {
        if (!engine.rules[i].compiled) continue;
        int ret = regexec(&engine.rules[i].regex, text, 0, NULL, 0);
        if (ret == 0) {
            decision->flagged = 1;
            ModerationResult *res = &decision->results[decision->result_count++];
            res->category = engine.rules[i].category;
            res->matched_pattern = engine.rules[i].pattern_str;
            res->confidence = 0.95;
        }
    }
    pthread_mutex_unlock(&engine.mutex);
    return decision->flagged ? 1 : 0;
}

int moderation_check_prompt(const char *prompt, ModerationDecision *decision) {
    return moderation_check(prompt, decision);
}

int moderation_check_response(const char *response, ModerationDecision *decision) {
    return moderation_check(response, decision);
}

const char *moderation_category_name(ModerationCategory cat) {
    static const char *names[] = {
        "hate", "harassment", "sexual", "violence",
        "self_harm", "spam", "malicious_code", "pii"
    };
    if ((int)cat >= 0 && (int)cat < MODERATION_CATEGORY_COUNT)
        return names[cat];
    return "unknown";
}

void moderation_cleanup(void) {
    pthread_mutex_lock(&engine.mutex);
    for (int i = 0; i < engine.rule_count; i++) {
        if (engine.rules[i].compiled) {
            regfree(&engine.rules[i].regex);
        }
        free(engine.rules[i].pattern_str);
    }
    free(engine.rules);
    engine.rule_count = 0;
    engine.rule_capacity = 0;
    pthread_mutex_unlock(&engine.mutex);
    pthread_mutex_destroy(&engine.mutex);
    log_message("MODERATION", "Content moderation cleaned up");
}
