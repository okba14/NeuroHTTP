#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ai/ai_gateway.h"

static int test_cost_calculation(void) {
    CostAccount acc = {0};

    gateway_update_cost(&acc, "gpt-4o", 100, 50);
    assert(acc.total_input_tokens == 100);
    assert(acc.total_output_tokens == 50);
    assert(acc.total_requests == 1);
    assert(acc.total_cost_usd > 0.0);

    double prev_cost = acc.total_cost_usd;
    gateway_update_cost(&acc, "gpt-4o", 100, 50);
    assert(acc.total_requests == 2);
    assert(acc.total_cost_usd > prev_cost);

    printf("[PASS] test_cost_calculation\n");
    return 0;
}

static int test_embeddings_payload(void) {
    const char *inputs[] = {"hello world", "test input"};
    char output[2048];
    int ret = gateway_build_embeddings_payload("text-embedding-3-small", inputs, 2, output, sizeof(output));
    assert(ret == 0);
    assert(strstr(output, "text-embedding-3-small") != NULL);
    assert(strstr(output, "hello world") != NULL);
    assert(strstr(output, "test input") != NULL);

    printf("[PASS] test_embeddings_payload\n");
    return 0;
}

static int test_vision_content(void) {
    const char *images[] = {"data:image/png;base64,iVBORw0K"};
    char output[2048];
    int ret = gateway_build_vision_content("describe this image", images, 1, output, sizeof(output));
    assert(ret == 0);
    assert(strstr(output, "describe this image") != NULL);
    assert(strstr(output, "image_url") != NULL);
    assert(strstr(output, "data:image/png") != NULL);

    printf("[PASS] test_vision_content\n");
    return 0;
}

static int test_tool_config(void) {
    ToolConfig tools = {0};
    int ret = gateway_add_tool_function(&tools, "get_weather", "Get weather for a city",
        "{\"type\": \"object\", \"properties\": {\"city\": {\"type\": \"string\"}}}");
    assert(ret == 0);
    assert(tools.function_count == 1);
    assert(strcmp(tools.functions[0].name, "get_weather") == 0);

    ret = gateway_add_tool_function(&tools, "search", "Search the web",
        "{\"type\": \"object\", \"properties\": {\"query\": {\"type\": \"string\"}}}");
    assert(ret == 0);
    assert(tools.function_count == 2);

    gateway_free_tool_config(&tools);
    assert(tools.function_count == 0);

    printf("[PASS] test_tool_config\n");
    return 0;
}

static int test_embeddings_parse(void) {
    const char *response = "{\"data\": [{\"embedding\": [0.1, 0.2, 0.3, 0.4, 0.5]}], \"model\": \"test\"}";
    float emb[16];
    int dims = 0;
    int ret = gateway_parse_embeddings_response(response, emb, &dims, 16);
    assert(ret == 0);
    assert(dims == 5);
    assert(emb[0] == 0.1f);
    assert(emb[4] == 0.5f);

    printf("[PASS] test_embeddings_parse\n");
    return 0;
}

int main(void) {
    printf("=== AI Gateway Tests ===\n");
    test_cost_calculation();
    test_embeddings_payload();
    test_vision_content();
    test_tool_config();
    test_embeddings_parse();
    printf("=== All gateway tests PASSED ===\n");
    return 0;
}
