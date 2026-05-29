#ifndef AIONIC_PLUGIN_H
#define AIONIC_PLUGIN_H

#include "parser.h"

/* ===== Plugin Hook Points ===== */
typedef enum {
    PLUGIN_HOOK_PRE_REQUEST,     /* Before routing */
    PLUGIN_HOOK_POST_REQUEST,    /* After routing, before response */
    PLUGIN_HOOK_AI_PROMPT,       /* Before AI call */
    PLUGIN_HOOK_AI_RESPONSE,     /* After AI response */
    PLUGIN_HOOK_ON_CONNECT,      /* New connection */
    PLUGIN_HOOK_ON_DISCONNECT,   /* Connection closed */
    PLUGIN_HOOK_COUNT
} PluginHookPoint;

/* Plugin context passed to hooks */
typedef struct {
    void *request;
    void *response;
    const char *prompt;
    char *ai_response;
    size_t ai_response_size;
    int client_fd;
    const char *client_ip;
    void *user_data;
} PluginContext;

/* Plugin hook function signature */
typedef int (*PluginHookFunc)(PluginHookPoint point, PluginContext *ctx);

int plugin_init(const char *plugin_dir);
int plugin_load(const char *plugin_path);
int plugin_unload(const char *plugin_name);
int plugin_set_enabled(const char *plugin_name, int enabled);

/* Process all enabled plugins at a given hook point */
int plugin_process_hooks(PluginHookPoint point, PluginContext *ctx);

/* Direct request/response processing (legacy) */
int plugin_process_request(void *request, void *response);

int plugin_get_list(char ***plugin_names, int *count);
int plugin_install_from_url(const char *url);
int plugin_install_from_github(const char *repo, const char *asset_pattern);
void plugin_cleanup();

#endif // AIONIC_PLUGIN_H
