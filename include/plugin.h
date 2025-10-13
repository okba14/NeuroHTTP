#ifndef AIONIC_PLUGIN_H
#define AIONIC_PLUGIN_H


int plugin_init(const char *plugin_dir);
int plugin_load(const char *plugin_path);
int plugin_unload(const char *plugin_name);
int plugin_set_enabled(const char *plugin_name, int enabled);
int plugin_process_request(void *request, void *response);
int plugin_get_list(char ***plugin_names, int *count);
void plugin_cleanup();

#endif // AIONIC_PLUGIN_H
