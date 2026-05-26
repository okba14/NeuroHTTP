#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 
 * OpenAI Proxy Plugin
 * Intercepts AI prompts and adds custom system instructions
 */

static char *system_instruction = NULL;

int plugin_init(void) {
    system_instruction = strdup("You are a helpful AI assistant running on AIONIC server.");
    printf("[openai_proxy] Plugin initialized - AI prompt interceptor active\n");
    return 0;
}

int plugin_process(void *request, void *response) {
    (void)request;
    (void)response;
    return 0;
}

int plugin_hook(int hook_point, void *ctx) {
    switch (hook_point) {
        case 2: { /* PLUGIN_HOOK_AI_PROMPT - Before AI call */
            /* Modify prompt or add context */
            printf("[openai_proxy] AI prompt intercepted\n");
            break;
        }
        case 3: { /* PLUGIN_HOOK_AI_RESPONSE - After AI response */
            printf("[openai_proxy] AI response received\n");
            break;
        }
        default:
            break;
    }
    return 0;
}

void plugin_cleanup(void) {
    free(system_instruction);
    printf("[openai_proxy] Plugin cleaned up\n");
}
