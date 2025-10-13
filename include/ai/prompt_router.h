#ifndef AIONIC_AI_PROMPT_ROUTER_H
#define AIONIC_AI_PROMPT_ROUTER_H


int prompt_router_init();
int prompt_router_add_model(const char *name, const char *api_endpoint, int max_tokens, float temperature);
int prompt_router_remove_model(const char *name);
int prompt_router_set_default_model(const char *name);
int prompt_router_route(const char *prompt, const char *model_name, char *response, size_t response_size);
int prompt_router_get_models(char ***model_names, int *count);
int prompt_router_set_model_availability(const char *name, int is_available);
void prompt_router_cleanup();

#endif // AIONIC_AI_PROMPT_ROUTER_H
