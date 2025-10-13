#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "plugin.h"
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <string.h>
#include <stdlib.h>

// تعريف بنئة الموديول
typedef struct {
    char *name;
    char *path;
    void *handle;
    int (*init)(void);
    int (*process)(void *, void *);
    void (*cleanup)(void);
    int is_loaded;
    int is_enabled;
} Plugin;

// تعريف بنئة مدير الموديولات
typedef struct {
    Plugin *plugins;
    int plugin_count;
    int plugin_capacity;
    pthread_mutex_t mutex;
    char *plugin_dir;
} PluginManager;

static PluginManager global_plugin_manager;

// دالة لتحميل موديول واحد
static int load_plugin(const char *plugin_path) {
    void *handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        log_message("PLUGIN", dlerror());
        return -1;
    }
    
    // استخراج الرموز المطلوبة
    int (*init)(void) = dlsym(handle, "plugin_init");
    int (*process)(void *, void *) = dlsym(handle, "plugin_process");
    void (*cleanup)(void) = dlsym(handle, "plugin_cleanup");
    
    if (!init || !process || !cleanup) {
        log_message("PLUGIN", "Plugin missing required functions");
        dlclose(handle);
        return -1;
    }
    
    // إضافة الموديول إلى القائمة
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    if (global_plugin_manager.plugin_count >= global_plugin_manager.plugin_capacity) {
        // توسيع سعة الموديولات
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
    
    // استخراج اسم الموديول من المسار
    const char *filename = strrchr(plugin_path, '/');
    if (!filename) filename = plugin_path;
    else filename++;
    
    // إزالة الامتداد
    char *name = strdup(filename);
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    
    // تهيئة بنية الموديول
    Plugin *plugin = &global_plugin_manager.plugins[global_plugin_manager.plugin_count];
    plugin->name = name;
    plugin->path = strdup(plugin_path);
    plugin->handle = handle;
    plugin->init = init;
    plugin->process = process;
    plugin->cleanup = cleanup;
    plugin->is_loaded = 1;
    plugin->is_enabled = 1;
    
    // استدعاء دالة التهيئة
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

// دالة لتحميل جميع الموديولات من دليل
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
        // تخطي الدلائل الحالية والوالدة
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // التحقق من أن الملف هو مكتبة مشتركة (.so)
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

// تهيئة مدير الموديولات
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
    
    // تحميل الموديولات من الدليل
    load_plugins_from_directory(global_plugin_manager.plugin_dir);
    
    log_message("PLUGIN", "Plugin manager initialized");
    return 0;
}

// تحميل موديول معين
int plugin_load(const char *plugin_path) {
    return load_plugin(plugin_path);
}

// تفريغ موديول معين
int plugin_unload(const char *plugin_name) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    for (int i = 0; i < global_plugin_manager.plugin_count; i++) {
        if (strcmp(global_plugin_manager.plugins[i].name, plugin_name) == 0) {
            Plugin *plugin = &global_plugin_manager.plugins[i];
            
            if (plugin->is_loaded) {
                // استدعاء دالة التنظيف
                plugin->cleanup();
                
                // إغلاق المكتبة
                dlclose(plugin->handle);
                
                // تحرير الموارد
                free(plugin->name);
                free(plugin->path);
                
                // إزالة الموديول من القائمة
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

// تمكين أو تعطيل موديول
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

// معالجة طلب عبر جميع الموديولات الممكنة
int plugin_process_request(void *request, void *response) {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    int result = 0;
    
    for (int i = 0; i < global_plugin_manager.plugin_count; i++) {
        Plugin *plugin = &global_plugin_manager.plugins[i];
        
        if (plugin->is_loaded && plugin->is_enabled) {
            int plugin_result = plugin->process(request, response);
            if (plugin_result != 0) {
                result = plugin_result;
                
                char log_msg[256];
                snprintf(log_msg, sizeof(log_msg), "Plugin %s returned error: %d", 
                        plugin->name, plugin_result);
                log_message("PLUGIN", log_msg);
                
                // يمكننا الاستمرار مع الموديولات الأخرى أو التوقف هنا
                // break;
            }
        }
    }
    
    pthread_mutex_unlock(&global_plugin_manager.mutex);
    return result;
}

// الحصول على قائمة الموديولات المحملة
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

// تنظيف مدير الموديولات
void plugin_cleanup() {
    pthread_mutex_lock(&global_plugin_manager.mutex);
    
    // تفريغ جميع الموديولات
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
