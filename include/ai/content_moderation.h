#ifndef AIONIC_CONTENT_MODERATION_H
#define AIONIC_CONTENT_MODERATION_H

#include <stddef.h>

typedef enum {
    MODERATION_CATEGORY_HATE,
    MODERATION_CATEGORY_HARASSMENT,
    MODERATION_CATEGORY_SEXUAL,
    MODERATION_CATEGORY_VIOLENCE,
    MODERATION_CATEGORY_SELF_HARM,
    MODERATION_CATEGORY_SPAM,
    MODERATION_CATEGORY_MALICIOUS_CODE,
    MODERATION_CATEGORY_PII,
    MODERATION_CATEGORY_COUNT
} ModerationCategory;

typedef struct {
    ModerationCategory category;
    const char *matched_pattern;
    double confidence;
} ModerationResult;

typedef struct {
    int flagged;
    ModerationResult results[16];
    int result_count;
} ModerationDecision;

int moderation_init(const char *custom_patterns_file);
int moderation_check_prompt(const char *prompt, ModerationDecision *decision);
int moderation_check_response(const char *response, ModerationDecision *decision);
int moderation_add_pattern(ModerationCategory category, const char *pattern);
void moderation_cleanup(void);

const char *moderation_category_name(ModerationCategory cat);

#endif
