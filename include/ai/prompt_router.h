#ifndef AIONIC_AI_PROMPT_ROUTER_H
#define AIONIC_AI_PROMPT_ROUTER_H

#include <stdint.h>
#include "config.h"

#define AI_TIER_FAST      1
#define AI_TIER_BALANCED  2
#define AI_TIER_REASONING 3
#define AI_TIER_PREMIUM   4

typedef enum {
    MODEL_STATE_HEALTHY = 0,
    MODEL_STATE_DEGRADED,
    MODEL_STATE_RATE_LIMITED,
    MODEL_STATE_DECOMMISSIONED,
    MODEL_STATE_QUARANTINED,
    MODEL_STATE_DISABLED,
    MODEL_STATE_MISSING_KEY
} ModelState;

typedef struct {
    ModelState state;
    int consecutive_failures;
    int consecutive_successes;
    time_t last_failure_time;
    time_t last_success_time;
    time_t quarantined_until;
    int circuit_open;
    time_t circuit_open_until;
    int circuit_failures;
    uint64_t circuit_window_start_us;
} ModelHealth;

typedef enum {
    AI_ERROR_NONE = 0,
    AI_ERROR_API_FAILURE,
    AI_ERROR_TIMEOUT,
    AI_ERROR_RATE_LIMIT,
    AI_ERROR_DECOMMISSIONED,
    AI_ERROR_AUTH,
    AI_ERROR_INVALID_MODEL,
    AI_ERROR_QUARANTINED,
    AI_ERROR_CIRCUIT_OPEN,
    AI_ERROR_MEMORY
} AIErrorCode;

typedef uint64_t CapabilityFlags;
#define CAP_STREAMING    (1 << 0)
#define CAP_VISION       (1 << 1)
#define CAP_TOOL_USE     (1 << 2)
#define CAP_REASONING    (1 << 3)
#define CAP_JSON_MODE    (1 << 4)
#define CAP_FUNCTION_CALL (1 << 5)

typedef struct {
    uint64_t total_input_tokens;
    uint64_t total_output_tokens;
    uint64_t total_cost_usd;  // micro-dollars
    uint64_t total_requests;
    uint64_t total_errors;
} TokenAccount;

int prompt_router_init(void);
int prompt_router_init_with_config(const Config *config);
int prompt_router_add_model(const char *name, const char *api_endpoint, const char *api_key_env, int max_tokens, float temperature, int tier);
int prompt_router_add_model_ex(const char *name, const char *api_endpoint, const char *api_key_env, int max_tokens, float temperature, int tier, CapabilityFlags caps);
int prompt_router_remove_model(const char *name);
int prompt_router_set_default_model(const char *name);
int prompt_router_route(const char *prompt, const char *model_name, char *response, size_t response_size, char *actual_model, size_t actual_model_size);
int prompt_router_route_with_options(const char *prompt, const char *model_name, char *response, size_t response_size,
                                      char *actual_model, size_t actual_model_size, int min_tier, int max_cost_cents);
int prompt_router_get_models(char ***model_names, int *count);
int prompt_router_get_available_count(void);
int prompt_router_set_model_availability(const char *name, int is_available);
int prompt_router_set_model_state(const char *name, ModelState state);
int prompt_router_get_model_health(const char *name, ModelHealth *health);
int prompt_router_get_token_account(TokenAccount *account);
int prompt_router_get_model_cost_account(const char *name, double *cost_usd);
int prompt_router_hot_reload(void);
void prompt_router_cleanup(void);

#endif
