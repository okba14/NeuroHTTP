#ifndef AIONIC_AI_PROMPT_ROUTER_H
#define AIONIC_AI_PROMPT_ROUTER_H

/**
 * Initializes the Prompt Router.
 * Loads default AI models and initializes the network library (libcurl).
 * 
 * @return 0 on success, -1 on failure.
 */
int prompt_router_init();

/**
 * Adds a new AI model to the routing table.
 * 
 * @param name The name of the model (e.g., "gpt-3.5-turbo").
 * @param api_endpoint The full URL of the API (e.g., "https://api.openai.com/v1/...").
 * @param max_tokens Maximum tokens for the response.
 * @param temperature Sampling temperature (0.0 to 1.0).
 * @return 0 on success, -1 on failure.
 */
int prompt_router_add_model(const char *name, const char *api_endpoint, int max_tokens, float temperature);

/**
 * Removes an AI model from the routing table by name.
 * 
 * @param name The name of the model to remove.
 * @return 0 on success, -1 if model not found.
 */
int prompt_router_remove_model(const char *name);

/**
 * Sets the default AI model to be used when no specific model is requested.
 * 
 * @param name The name of the model to set as default.
 * @return 0 on success, -1 if model not found.
 */
int prompt_router_set_default_model(const char *name);

/**
 * Routes a user prompt to the specified AI model (or default) and returns the response.
 * This function handles the actual HTTP request and JSON parsing internally.
 * 
 * @param prompt The input text prompt from the user.
 * @param model_name The specific model to use (NULL to use default).
 * @param response Buffer to store the AI's response.
 * @param response_size Size of the response buffer.
 * @return 0 on success, -1 on failure.
 */
int prompt_router_route(const char *prompt, const char *model_name, char *response, size_t response_size);

/**
 * Retrieves a list of names of all available AI models.
 * The caller is responsible for freeing the allocated memory.
 * 
 * @param model_names Pointer to an array of strings (output).
 * @param count Pointer to store the number of models.
 * @return 0 on success, -1 on failure.
 */
int prompt_router_get_models(char ***model_names, int *count);

/**
 * Sets the availability status of a specific model.
 * 
 * @param name The name of the model.
 * @param is_available 1 if available, 0 if disabled.
 * @return 0 on success, -1 if model not found.
 */
int prompt_router_set_model_availability(const char *name, int is_available);

/**
 * Cleans up the Prompt Router resources.
 * Frees memory and shuts down the network library.
 */
void prompt_router_cleanup();

#endif // AIONIC_AI_PROMPT_ROUTER_H
