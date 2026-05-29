#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ai/ai_gateway.h"
#include "parser.h"
#include "utils.h"

int gateway_add_tool_function(ToolConfig *tools, const char *name, const char *description, const char *parameters_json) {
    if (!tools || !name) return -1;
    if (tools->function_count >= tools->function_capacity) {
        int new_cap = tools->function_capacity == 0 ? 8 : tools->function_capacity * 2;
        FunctionDefinition *new_fns = realloc(tools->functions, sizeof(FunctionDefinition) * new_cap);
        if (!new_fns) return -1;
        memset(new_fns + tools->function_capacity, 0, sizeof(FunctionDefinition) * (new_cap - tools->function_capacity));
        tools->functions = new_fns;
        tools->function_capacity = new_cap;
    }
    FunctionDefinition *fn = &tools->functions[tools->function_count];
    fn->name = strdup(name);
    fn->description = description ? strdup(description) : strdup("");
    fn->parameters_json = parameters_json ? strdup(parameters_json) : strdup("{}");
    if (!fn->name || !fn->description || !fn->parameters_json) {
        free(fn->name); free(fn->description); free(fn->parameters_json);
        return -1;
    }
    tools->function_count++;
    return 0;
}

void gateway_free_tool_config(ToolConfig *tools) {
    if (!tools) return;
    for (int i = 0; i < tools->function_count; i++) {
        free(tools->functions[i].name);
        free(tools->functions[i].description);
        free(tools->functions[i].parameters_json);
    }
    free(tools->functions);
    memset(tools, 0, sizeof(ToolConfig));
}

int gateway_build_vision_content(const char *text, const char **image_urls, int image_count,
                                  char *output, size_t output_size) {
    if (!output || output_size == 0) return -1;
    size_t pos = 0;
    pos += snprintf(output + pos, output_size - pos,
        "[{\"type\": \"text\", \"text\": \"%s\"}", text ? text : "");
    for (int i = 0; i < image_count && pos < output_size; i++) {
        pos += snprintf(output + pos, output_size - pos,
            ", {\"type\": \"image_url\", \"image_url\": {\"url\": \"%s\"}}",
            image_urls[i] ? image_urls[i] : "");
    }
    if (pos < output_size) {
        pos += snprintf(output + pos, output_size - pos, "]");
    }
    return (pos < output_size) ? 0 : -1;
}

int gateway_build_embeddings_payload(const char *model, const char **inputs, int input_count,
                                      char *output, size_t output_size) {
    if (!model || !inputs || input_count <= 0 || !output || output_size == 0) return -1;
    size_t pos = 0;
    pos += snprintf(output + pos, output_size - pos,
        "{\"model\": \"%s\", \"input\": [", model);
    for (int i = 0; i < input_count && pos < output_size; i++) {
        if (i > 0) pos += snprintf(output + pos, output_size - pos, ", ");
        pos += snprintf(output + pos, output_size - pos, "\"%s\"", inputs[i] ? inputs[i] : "");
    }
    if (pos < output_size) {
        pos += snprintf(output + pos, output_size - pos, "]}");
    }
    return (pos < output_size) ? 0 : -1;
}

int gateway_parse_embeddings_response(const char *raw, float *embeddings, int *dimensions, int max_dimensions) {
    if (!raw || !embeddings || !dimensions || max_dimensions <= 0) return -1;
    const char *data_start = strstr(raw, "\"data\"");
    if (!data_start) return -1;
    const char *embedding_start = strstr(data_start, "\"embedding\"");
    if (!embedding_start) return -1;
    const char *bracket = strchr(embedding_start, '[');
    if (!bracket) return -1;
    bracket++;
    int dim = 0;
    const char *p = bracket;
    while (*p && *p != ']' && dim < max_dimensions) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (*p == ']' || !*p) break;
        char *end;
        double val = strtod(p, &end);
        if (end == p) break;
        embeddings[dim++] = (float)val;
        p = end;
    }
    *dimensions = dim;
    return dim > 0 ? 0 : -1;
}

int gateway_handle_tool_calls(const char *response_body, char *ai_response, size_t ai_response_size, ToolConfig *tools) {
    (void)tools;
    if (!response_body || !ai_response || ai_response_size == 0) return -1;
    const char *tool_calls = strstr(response_body, "\"tool_calls\"");
    if (!tool_calls) return -1;
    const char *function_name = NULL, *arguments = NULL;
    const char *name_key = strstr(tool_calls, "\"name\"");
    if (name_key) {
        name_key += 6;
        while (*name_key && *name_key != '"') name_key++;
        if (*name_key) {
            name_key++;
            const char *name_end = strchr(name_key, '"');
            if (name_end) {
                size_t nlen = name_end - name_key;
                if (nlen < 128) {
                    char fname[128];
                    memcpy(fname, name_key, nlen);
                    fname[nlen] = '\0';
                    function_name = strdup(fname);
                }
            }
        }
    }
    const char *args_key = strstr(tool_calls, "\"arguments\"");
    if (args_key) {
        args_key += 11;
        while (*args_key && *args_key != '"') args_key++;
        if (*args_key) {
            args_key++;
            const char *args_end = strchr(args_key, '"');
            if (args_end) {
                size_t alen = args_end - args_key;
                if (alen < 2048) {
                    char fargs[2048];
                    memcpy(fargs, args_key, alen);
                    fargs[alen] = '\0';
                    arguments = strdup(fargs);
                }
            }
        }
    }
    if (function_name) {
        snprintf(ai_response, ai_response_size,
            "{\"tool_called\": \"%s\", \"arguments\": %s, \"result\": \"executed\"}",
            function_name, arguments ? arguments : "{}");
        free((void*)function_name);
        free((void*)arguments);
        return 0;
    }
    return -1;
}

void gateway_update_cost(CostAccount *account, const char *model_name, int input_tokens, int output_tokens) {
    if (!account || !model_name) return;
    double input_rate = 0.0, output_rate = 0.0;
    if (strstr(model_name, "gpt-4o")) {
        input_rate = 2.50; output_rate = 10.00;
    } else if (strstr(model_name, "gpt-4-turbo")) {
        input_rate = 10.00; output_rate = 30.00;
    } else if (strstr(model_name, "gpt-4")) {
        input_rate = 30.00; output_rate = 60.00;
    } else if (strstr(model_name, "gpt-3.5") || strstr(model_name, "gpt-35")) {
        input_rate = 0.50; output_rate = 1.50;
    } else if (strstr(model_name, "claude-3-5-sonnet")) {
        input_rate = 3.00; output_rate = 15.00;
    } else if (strstr(model_name, "claude-3-opus")) {
        input_rate = 15.00; output_rate = 75.00;
    } else if (strstr(model_name, "claude-3-haiku")) {
        input_rate = 0.25; output_rate = 1.25;
    } else if (strstr(model_name, "gemini-1.5-pro")) {
        input_rate = 1.25; output_rate = 5.00;
    } else if (strstr(model_name, "gemini-2.0") || strstr(model_name, "gemini-1.5-flash")) {
        input_rate = 0.10; output_rate = 0.40;
    } else if (strstr(model_name, "llama") || strstr(model_name, "mixtral") || strstr(model_name, "gemma")) {
        input_rate = 0.59; output_rate = 0.79;
    } else if (strstr(model_name, "deepseek")) {
        input_rate = 0.14; output_rate = 0.28;
    } else if (strstr(model_name, "mistral-large")) {
        input_rate = 2.00; output_rate = 6.00;
    } else {
        input_rate = 1.00; output_rate = 2.00;
    }
    account->total_input_tokens += input_tokens;
    account->total_output_tokens += output_tokens;
    account->total_cost_usd += (input_tokens / 1000.0 * input_rate) + (output_tokens / 1000.0 * output_rate);
    account->total_requests++;
}
