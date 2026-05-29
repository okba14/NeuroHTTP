#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/plugin.h"

int plugin_init(PluginInfo *info) {
    if (!info) return -1;
    info->name = "limiter";
    info->version = "1.0.0";
    info->hooks = PLUGIN_HOOK_PRE_REQUEST;
    return 0;
}

int plugin_hook(int hook_id, PluginContext *ctx) {
    (void)hook_id;
    (void)ctx;
    return 0;
}

void plugin_cleanup(void) {
}
