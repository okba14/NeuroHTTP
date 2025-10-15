#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>  // Important for nanosleep
#include "server.h"
#include "config.h"
#include "parser.h"
#include "firewall.h"
#include "optimizer.h"
#include "cache.h"
#include "plugin.h"
#include "ai/prompt_router.h"
#include "ai/tokenizer.h"
#include "ai/stats.h"

// Global variable to control server running state
volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    running = 0;
}

int main(int argc, char *argv[]) {
    (void)argc;  // Avoid unused parameter warning
    (void)argv;  // Avoid unused parameter warning
    
    printf("========================================\n");
    printf("    AIONIC AI Web Server v1.0\n");
    printf("========================================\n");
    
    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("Starting AIONIC Server...\n");
    
    // Load configuration
    Config config;
    if (load_config("config/aionic.conf", &config) != 0) {
        fprintf(stderr, "❌ Failed to load configuration\n");
        return 1;
    }
    
    printf("✅ Configuration loaded successfully\n");
    printf("   - Port: %d\n", config.port);
    printf("   - Threads: %d\n", config.thread_count);
    printf("   - Max Connections: %d\n", config.max_connections);
    
    // Initialize cache
    if (config.enable_cache && cache_init(config.cache_size, config.cache_ttl) != 0) {
        fprintf(stderr, "❌ Failed to initialize cache\n");
        free_config(&config);
        return 1;
    }
    
    if (config.enable_cache) {
        printf("✅ Cache initialized (%d entries, %d TTL)\n", config.cache_size, config.cache_ttl);
    }
    
    // Initialize firewall
    if (config.enable_firewall && firewall_init(&config) != 0) {
        fprintf(stderr, "❌ Failed to initialize firewall\n");
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    if (config.enable_firewall) {
        printf("✅ Firewall initialized\n");
    }
    
    // Initialize optimizer
    if (config.enable_optimization && optimizer_init(&config) != 0) {
        fprintf(stderr, "❌ Failed to initialize optimizer\n");
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    if (config.enable_optimization) {
        printf("✅ Optimizer initialized\n");
    }
    
    // Initialize AI prompt router
    if (prompt_router_init() != 0) {
        fprintf(stderr, "❌ Failed to initialize AI prompt router\n");
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    printf("✅ AI prompt router initialized\n");
    
    // Initialize tokenizer
    if (tokenizer_init() != 0) {
        fprintf(stderr, "❌ Failed to initialize tokenizer\n");
        prompt_router_cleanup();
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    printf("✅ Tokenizer initialized\n");
    
    // Initialize stats collector
    if (stats_init("stats.json", 300) != 0) {
        fprintf(stderr, "❌ Failed to initialize stats collector\n");
        tokenizer_cleanup();
        prompt_router_cleanup();
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    printf("✅ Stats collector initialized\n");
    
    // Initialize plugin system
    if (plugin_init("plugins") != 0) {
        fprintf(stderr, "❌ Failed to initialize plugin system\n");
        stats_cleanup();
        tokenizer_cleanup();
        prompt_router_cleanup();
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    printf("✅ Plugin system initialized\n");
    
    // Create and start server
    Server server;
    if (server_init(&server, &config) != 0) {
        fprintf(stderr, "❌ Failed to initialize server\n");
        plugin_cleanup();
        stats_cleanup();
        tokenizer_cleanup();
        prompt_router_cleanup();
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    if (server_start(&server) != 0) {
        fprintf(stderr, "❌ Failed to start server\n");
        server_cleanup(&server);
        plugin_cleanup();
        stats_cleanup();
        tokenizer_cleanup();
        prompt_router_cleanup();
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    printf("✅ Server started successfully\n");
    printf("🚀 AIONIC Server is running on http://localhost:%d\n", config.port);
    printf("   Press Ctrl+C to stop the server\n");
    printf("========================================\n");
    
    // Main server loop
    while (running) {
        server_process_events(&server);
        if (config.enable_optimization) {
            optimizer_run(&server);
        }
        stats_auto_save();
        
        // Use nanosleep instead of usleep
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000; // 100ms = 100,000,000 nanoseconds
        nanosleep(&ts, NULL);
    }
    
    printf("\n🛑 Shutting down AIONIC Server...\n");
    
    // Clean up resources on exit
    server_stop(&server);
    server_cleanup(&server);
    plugin_cleanup();
    stats_cleanup();
    tokenizer_cleanup();
    prompt_router_cleanup();
    if (config.enable_optimization) optimizer_cleanup();
    if (config.enable_firewall) firewall_cleanup();
    if (config.enable_cache) cache_cleanup();
    free_config(&config);
    
    printf("✅ AIONIC Server stopped gracefully\n");
    printf("========================================\n");
    
    return 0;
}
