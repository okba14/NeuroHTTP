#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Plugin entry points */
int plugin_init(void) {
    printf("[logstats] Plugin initialized - request logger active\n");
    return 0;
}

int plugin_process(void *request, void *response) {
    (void)response;
    /* Legacy process - actual logic in hooks */
    return 0;
}

int plugin_hook(int hook_point, void *ctx) {
    /* Context contains: request, response, prompt, ai_response, etc */
    (void)ctx;
    
    switch (hook_point) {
        case 0: { /* PLUGIN_HOOK_PRE_REQUEST */
            /* Log requests to stdout */
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
            printf("[logstats:%s] Request intercepted by plugin\n", buf);
            break;
        }
        case 2: { /* PLUGIN_HOOK_AI_PROMPT */
            printf("[logstats] AI prompt being processed\n");
            break;
        }
        default:
            break;
    }
    return 0;
}

void plugin_cleanup(void) {
    printf("[logstats] Plugin cleaned up\n");
}
