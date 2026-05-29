#define _POSIX_C_SOURCE 200809L

// ====== Standard Library Headers ======
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>

// ====== Project Headers ======
#include "plugin.h"
#include "utils.h"
#include "asm_utils.h"


typedef struct {
    char *name;
    char *path;
    void *handle;
    int (*init)(void);
    int (*process)(void *, void *);
    void (*cleanup)(void);
    int (*hook_func)(int, void *);        /* New hook system */
    int is_loaded;
    int is_enabled;
} Plugin;

typedef struct {
    Plugin *plugins;
    int plugin_count;
    int plugin_capacity;
    pthread_mutex_t mutex;
    char *plugin_dir;
} PluginManager;

static PluginManager global_plugin_manager;

static int load_plugin(const char *plugin_path) {
    void *handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        log_message("PLUGIN", dlerror());
        return -1;
    }
    
    int (*init)(void) = dlsym(handle, "plugin_init");
    int (*process)(void *, void *) = dlsym(handle, "plugin_process");
    void (*cleanup)(void) = dlsym(handle, "plugin_cleanup");
    int (*hook_func)(int, void *) = dlsym(handle, "plugin_hook");
    
    if (!init || !cleanup) {
        log_message("PLUGIN", "Plugin missing required functions (plugin_init, plugin_cleanup)");
        dlclose(handle);
        return -1;
    }
    
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    if (global_plugin_manager.plugin_count >= global_plugin_manager.plugin_capacity) {
        int new_capacity = global_plugin_manager.plugin_capacity * 2;
        Plugin *new_plugins = realloc(global_plugin_manager.plugins, 
                                     sizeof(Plugin) * new_capacity);
        if (!new_plugins) {
            pthread_mutex_unlock(&global_plugin_manager.mutex);
            dlclose(handle);
            return -1;
        }
        
        global_plugin_manager.plugins = new_plugins;
        global_plugin_manager.plugin_capacity = new_capacity;
    }
    
    const char *filename = strrchr(plugin_path, '/');
    if (!filename) filename = plugin_path;
    else filename++;
    
    char *name = strdup(filename);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    
    Plugin *plugin = &global_plugin_manager.plugins[global_plugin_manager.plugin_count];
    plugin->name = name;
    plugin->path = strdup(plugin_path);
    plugin->handle = handle;
    plugin->init = init;
    plugin->process = process;
    plugin->cleanup = cleanup;
    plugin->hook_func = hook_func;
    plugin->is_loaded = 1;
    plugin->is_enabled = 1;
    
    if (plugin->init() != 0) {
        log_message("PLUGIN", "Plugin initialization failed");
        dlclose(handle);
        free(plugin->name);
        free(plugin->path);
        pthread_mutex_unlock(&global_plugin_manager.mutex);
        return -1;
    }
    
    global_plugin_manager.plugin_count++;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Plugin loaded: %s", plugin->name);
    log_message("PLUGIN", log_msg);
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return 0;
}

static int load_plugins_from_directory(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    
    dir = opendir(dir_path);
    if (!dir) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Cannot open plugin directory: %s", dir_path);
        log_message("PLUGIN", log_msg);
        return -1;
    }
    
    int loaded_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
            char plugin_path[1024];
            snprintf(plugin_path, sizeof(plugin_path), "%s/%s", dir_path, entry->d_name);
            
            if (load_plugin(plugin_path) == 0) {
                loaded_count++;
            }
        }
    }
    
    closedir(dir);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Loaded %d plugins from %s", loaded_count, dir_path);
    log_message("PLUGIN", log_msg);
    
    return loaded_count;
}

int plugin_init(const char *plugin_dir) {
    global_plugin_manager.plugin_capacity = 16;
    global_plugin_manager.plugins = calloc(global_plugin_manager.plugin_capacity, sizeof(Plugin));
    if (!global_plugin_manager.plugins) {
        return -1;
    }
    
    global_plugin_manager.plugin_count = 0;
    global_plugin_manager.plugin_dir = strdup(plugin_dir ? plugin_dir : "plugins");
    
    if (pthread_mutex_init(&global_plugin_manager.mutex, NULL) != 0) {
        free(global_plugin_manager.plugins);
        free(global_plugin_manager.plugin_dir);
        return -1;
    }
    
    load_plugins_from_directory(global_plugin_manager.plugin_dir);
    
    log_message("PLUGIN", "Plugin manager initialized");
    return 0;
}

int plugin_load(const char *plugin_path) {
    return load_plugin(plugin_path);
}

int plugin_unload(const char *plugin_name) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    for (int i = 0; i < global_plugin_manager.plugin_count; i++) {
        if (strcmp(global_plugin_manager.plugins[i].name, plugin_name) == 0) {
            Plugin *plugin = &global_plugin_manager.plugins[i];
            
            if (plugin->is_loaded) {
                plugin->cleanup();
                
                dlclose(plugin->handle);
                
                free(plugin->name);
                free(plugin->path);
                
                for (int j = i; j < global_plugin_manager.plugin_count - 1; j++) {
                    global_plugin_manager.plugins[j] = global_plugin_manager.plugins[j + 1];
                }
                
                global_plugin_manager.plugin_count--;
                
                char log_msg[256];
                snprintf(log_msg, sizeof(log_msg), "Plugin unloaded: %s", plugin_name);
                log_message("PLUGIN", log_msg);
                
                pthread_mutex_unlock(&global_plugin_manager.mutex);
                return 0;
            }
            
            break;
        }
    }
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return -1;
}

int plugin_set_enabled(const char *plugin_name, int enabled) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    for (int i = 0; i < global_plugin_manager.plugin_count; i++) {
        if (strcmp(global_plugin_manager.plugins[i].name, plugin_name) == 0) {
            global_plugin_manager.plugins[i].is_enabled = enabled;
            
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Plugin %s: %s", 
                    plugin_name, enabled ? "enabled" : "disabled");
            log_message("PLUGIN", log_msg);
            
            pthread_mutex_unlock(&global_plugin_manager.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return -1;
}

int plugin_process_request(void *request, void *response) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    int result = 0;
    
    for (int i = 0; i < global_plugin_manager.plugin_count; i++) {
        Plugin *plugin = &global_plugin_manager.plugins[i];
        
        if (plugin->is_loaded && plugin->is_enabled && plugin->process) {
            int plugin_result = plugin->process(request, response);
            if (plugin_result != 0) {
                result = plugin_result;
                
                char log_msg[256];
                snprintf(log_msg, sizeof(log_msg), "Plugin %s returned error: %d", 
                        plugin->name, plugin_result);
                log_message("PLUGIN", log_msg);
            }
        }
    }
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return result;
}

int plugin_process_hooks(PluginHookPoint point, PluginContext *ctx) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    int result = 0;
    
    for (int i = 0; i < global_plugin_manager.plugin_count; i++) {
        Plugin *plugin = &global_plugin_manager.plugins[i];
        
        if (plugin->is_loaded && plugin->is_enabled && plugin->hook_func) {
            int plugin_result = plugin->hook_func((int)point, ctx);
            if (plugin_result != 0) {
                result = plugin_result;
            }
        }
    }
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return result;
}

int plugin_get_list(char ***plugin_names, int *count) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    *count = global_plugin_manager.plugin_count;
    *plugin_names = malloc(sizeof(char *) * (*count));
    
    for (int i = 0; i < *count; i++) {
        (*plugin_names)[i] = strdup(global_plugin_manager.plugins[i].name);
    }
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return 0;
}

int plugin_install_from_url(const char *url) {
    if (!url) return -1;
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/aionic_plugin_XXXXXX.so");
    int fd = mkstemps(tmp_path, 3);
    if (fd < 0) return -1;

    char curl_cmd[2048];
    snprintf(curl_cmd, sizeof(curl_cmd), "curl -sL -o %s \"%s\"", tmp_path, url);
    int ret = system(curl_cmd);
    if (ret != 0) { close(fd); unlink(tmp_path); return -1; }

    ret = load_plugin(tmp_path);
    close(fd);
    if (ret != 0) { unlink(tmp_path); return -1; }

    char *plugin_dir = global_plugin_manager.plugin_dir;
    if (plugin_dir) {
        const char *name = strrchr(url, '/');
        if (name) name++; else name = url;
        char dest[1024];
        snprintf(dest, sizeof(dest), "%s/%s", plugin_dir, name);
        rename(tmp_path, dest);
    }

    log_message("PLUGIN", "Plugin installed from URL");
    return 0;
}

int plugin_install_from_github(const char *repo, const char *asset_pattern) {
    if (!repo || !asset_pattern) return -1;
    char api_url[512];
    snprintf(api_url, sizeof(api_url), "https://api.github.com/repos/%s/releases/latest", repo);
    char curl_cmd[2048];
    char tmp_json[1024];
    snprintf(tmp_json, sizeof(tmp_json), "/tmp/gh_release_XXXXXX.json");
    int fd = mkstemps(tmp_json, 5);
    if (fd < 0) return -1;
    close(fd);

    snprintf(curl_cmd, sizeof(curl_cmd),
        "curl -sL -H \"Accept: application/vnd.github.v3+json\" \"%s\" -o \"%s\"", api_url, tmp_json);
    int ret = system(curl_cmd);
    if (ret != 0) { unlink(tmp_json); return -1; }

    char *json_content = read_file(tmp_json);
    unlink(tmp_json);
    if (!json_content) return -1;

    char download_url[1024] = {0};
    const char *p = json_content;
    while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
        p += 22;
        while (*p && *p != '"') p++;
        if (*p) p++;
        const char *url_start = p;
        const char *url_end = strchr(p, '"');
        if (!url_end) break;
        size_t ulen = url_end - url_start;
        if (ulen < sizeof(download_url) - 1) {
            memcpy(download_url, url_start, ulen);
            download_url[ulen] = '\0';
            if (strstr(download_url, asset_pattern)) break;
        }
        p = url_end + 1;
    }
    free(json_content);

    if (download_url[0] == '\0') return -1;

    log_message("PLUGIN", "Downloading plugin from GitHub release");
    return plugin_install_from_url(download_url);
}

void plugin_cleanup() {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    for (int i = global_plugin_manager.plugin_count - 1; i >= 0; i--) {
        Plugin *plugin = &global_plugin_manager.plugins[i];
        
        if (plugin->is_loaded) {
            plugin->cleanup();
            dlclose(plugin->handle);
            free(plugin->name);
            free(plugin->path);
        }
    }
    
    free(global_plugin_manager.plugins);
    free(global_plugin_manager.plugin_dir);
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    pthread_mutex_destroy(&global_plugin_manager.mutex);
    
    log_message("PLUGIN", "Plugin manager cleaned up");
}
