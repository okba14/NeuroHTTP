#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>  // مهم لـ nanosleep
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

// متغير عام للتحكم في تشغيل الخادم
volatile sig_atomic_t running = 1;

// معالج إشارات الإغلاق الأنيق
void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    running = 0;
}

int main(int argc, char *argv[]) {
    (void)argc;  // تجنب تحذير المعلمة غير المستخدمة
    (void)argv;  // تجنب تحذير المعلمة غير المستخدمة
    
    printf("========================================\n");
    printf("    AIONIC AI Web Server v1.0\n");
    printf("========================================\n");
    
    // تسجيل معالجات الإشارات
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("Starting AIONIC Server...\n");
    
    // تحميل الإعدادات
    Config config;
    if (load_config("config/aionic.conf", &config) != 0) {
        fprintf(stderr, "❌ Failed to load configuration\n");
        return 1;
    }
    
    printf("✅ Configuration loaded successfully\n");
    printf("   - Port: %d\n", config.port);
    printf("   - Threads: %d\n", config.thread_count);
    printf("   - Max Connections: %d\n", config.max_connections);
    
    // تهيئة التخزين المؤقت
    if (config.enable_cache && cache_init(config.cache_size, config.cache_ttl) != 0) {
        fprintf(stderr, "❌ Failed to initialize cache\n");
        free_config(&config);
        return 1;
    }
    
    if (config.enable_cache) {
        printf("✅ Cache initialized (%d entries, %d TTL)\n", config.cache_size, config.cache_ttl);
    }
    
    // تهيئة جدار الحماية
    if (config.enable_firewall && firewall_init(&config) != 0) {
        fprintf(stderr, "❌ Failed to initialize firewall\n");
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    if (config.enable_firewall) {
        printf("✅ Firewall initialized\n");
    }
    
    // تهيئة المحسن
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
    
    // تهيئة موجه الطلبات الذكي
    if (prompt_router_init() != 0) {
        fprintf(stderr, "❌ Failed to initialize AI prompt router\n");
        if (config.enable_optimization) optimizer_cleanup();
        if (config.enable_firewall) firewall_cleanup();
        if (config.enable_cache) cache_cleanup();
        free_config(&config);
        return 1;
    }
    
    printf("✅ AI prompt router initialized\n");
    
    // تهيئة المميز
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
    
    // تهيئة جامع الإحصائيات
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
    
    // تهيئة نظام الموديولات
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
    
    // إنشاء وتشغيل الخادم
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
    
    // الحلقة الرئيسية للخادم
    while (running) {
        server_process_events(&server);
        if (config.enable_optimization) {
            optimizer_run(&server);
        }
        stats_auto_save();
        
        // استخدام nanosleep بدلاً من usleep
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000; // 100ms = 100,000,000 nanoseconds
        nanosleep(&ts, NULL);
    }
    
    printf("\n🛑 Shutting down AIONIC Server...\n");
    
    // تنظيف الموارد عند الخروج
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
