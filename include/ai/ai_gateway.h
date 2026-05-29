#ifndef AIONIC_AI_GATEWAY_H
#define AIONIC_AI_GATEWAY_H

#include <stddef.h>

typedef struct {
    char *name;
    char *description;
    char *parameters_json;
} FunctionDefinition;

typedef struct {
    FunctionDefinition *functions;
    int function_count;
    int function_capacity;
} ToolConfig;

typedef struct {
    int function_calling;
    int structured_output;
    int vision;
    int embeddings;
    int streaming;
} GatewayCapabilities;

typedef struct {
    double input_cost_per_1k;
    double output_cost_per_1k;
    double total_cost_usd;
    int total_input_tokens;
    int total_output_tokens;
    int total_requests;
    int total_errors;
} CostAccount;

typedef enum {
    RESPONSE_FORMAT_TEXT,
    RESPONSE_FORMAT_JSON_OBJECT,
    RESPONSE_FORMAT_JSON_SCHEMA
} ResponseFormatType;

typedef struct {
    ResponseFormatType type;
    char *json_schema;
} ResponseFormatConfig;

int gateway_add_tool_function(ToolConfig *tools, const char *name, const char *description, const char *parameters_json);
void gateway_free_tool_config(ToolConfig *tools);
int gateway_handle_tool_calls(const char *response_body, char *ai_response, size_t ai_response_size, ToolConfig *tools);
int gateway_build_vision_content(const char *text, const char **image_urls, int image_count,
                                  char *output, size_t output_size);
int gateway_build_embeddings_payload(const char *model, const char **inputs, int input_count,
                                      char *output, size_t output_size);
int gateway_parse_embeddings_response(const char *raw, float *embeddings, int *dimensions, int max_dimensions);
void gateway_update_cost(CostAccount *account, const char *model_name, int input_tokens, int output_tokens);

#endif
