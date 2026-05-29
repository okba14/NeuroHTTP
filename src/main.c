#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/inotify.h>
#include <stdarg.h>
#include <limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/epoll.h>
#include "config.h"
#include "server.h"
#include "parser.h"
#include "firewall.h"
#include "optimizer.h"
#include "cache.h"
#include "plugin.h"
#include "ai/prompt_router.h"
#include "ai/tokenizer.h"
#include "ai/stats.h"
#include "asm_utils.h"
#include "observability.h"
#include "arena.h"
#include "tls.h"
#include "http2.h"

#define AIONIC_VERSION_MAJOR 2
#define AIONIC_VERSION_MINOR 0
#define AIONIC_VERSION_PATCH 0
#define AIONIC_VERSION_STRING "2.0.0"
#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif
#ifndef BUILD_TIME
#define BUILD_TIME __TIME__
#endif
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#define CONFIG_WATCH_BUFFER_SIZE 4096

extern uint32_t crc32_asm(const void *data, size_t length);
extern uint32_t crc32_asm_avx2(const void *data, size_t length);

typedef enum {
    AIONIC_SUCCESS = 0, AIONIC_ERROR_CONFIG, AIONIC_ERROR_CACHE, AIONIC_ERROR_FIREWALL,
    AIONIC_ERROR_OPTIMIZER, AIONIC_ERROR_AI_ROUTER, AIONIC_ERROR_TOKENIZER,
    AIONIC_ERROR_STATS, AIONIC_ERROR_PLUGIN, AIONIC_ERROR_SERVER,
    AIONIC_ERROR_MEMORY, AIONIC_ERROR_SYSTEM
} AionicErrorCode;

typedef struct { AionicErrorCode code; const char *message; const char *file; int line; const char *function; } AionicError;

#define AIONIC_ERROR_CREATE(code, message) ((AionicError){code, message, __FILE__, __LINE__, __func__})

typedef enum { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARNING, LOG_LEVEL_ERROR, LOG_LEVEL_FATAL } LogLevel;
typedef struct { LogLevel level; FILE *output; int use_colors; } Logger;

typedef struct {
    int cache_initialized, firewall_initialized, optimizer_initialized;
    int ai_router_initialized, tokenizer_initialized, stats_initialized;
    int plugin_initialized, server_initialized, server_started;
} SystemState;

typedef struct { pthread_t *threads; int thread_count; } ThreadPool;
typedef struct { char **config_paths; int config_count; int config_capacity; } ConfigPaths;

typedef struct {
    Config config; Server server; Logger logger; SystemState state;
    ThreadPool thread_pool; ConfigPaths config_paths;
    int inotify_fd; int config_wd;
} AionicSystem;

static volatile sig_atomic_t running = 1;
static int signal_pipe[2] = {-1, -1};

static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        iouring_global_stop = 1;
        if (signal_pipe[1] >= 0) {
            char c = 1;
            write(signal_pipe[1], &c, 1);
        }
    } else if (sig == SIGHUP) {
        if (signal_pipe[1] >= 0) {
            char c = 2;
            write(signal_pipe[1], &c, 1);
        }
    } else if (sig == SIGUSR1) {
        if (signal_pipe[1] >= 0) {
            char c = 3;
            write(signal_pipe[1], &c, 1);
        }
    }
}

static void setup_signal_handling(void) {
    if (pipe(signal_pipe) < 0) { signal_pipe[0] = -1; signal_pipe[1] = -1; return; }
    fcntl(signal_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(signal_pipe[1], F_SETFL, O_NONBLOCK);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

static void print_version_info(void) {
    printf("========================================\n");
    printf("    AIONIC AI Web Server v%s\n", AIONIC_VERSION_STRING);
    printf("========================================\n");
    printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    printf("Git commit: %s\n", GIT_COMMIT);
    printf("Features: TLS1.3 HTTP/2 io_uring OCSP\n");
    printf("========================================\n");
}

static void logger_init(Logger *logger, LogLevel level, FILE *output, int use_colors) {
    logger->level = level; logger->output = output; logger->use_colors = use_colors;
}

static void logger_log(Logger *logger, LogLevel level, const char *format, ...) {
    if (level < logger->level) return;
    const char *level_str, *color_start = "", *color_end = "";
    if (logger->use_colors) {
        switch (level) {
            case LOG_LEVEL_DEBUG: color_start = "\033[36m"; break;
            case LOG_LEVEL_INFO: color_start = "\033[32m"; break;
            case LOG_LEVEL_WARNING: color_start = "\033[33m"; break;
            case LOG_LEVEL_ERROR: color_start = "\033[31m"; break;
            case LOG_LEVEL_FATAL: color_start = "\033[35m"; break;
        }
        color_end = "\033[0m";
    }
    switch (level) {
        case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case LOG_LEVEL_INFO: level_str = "INFO"; break;
        case LOG_LEVEL_WARNING: level_str = "WARNING"; break;
        case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
        case LOG_LEVEL_FATAL: level_str = "FATAL"; break;
    }
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    fputs(time_buffer, logger->output);
    fputs(" [", logger->output);
    fputs(level_str, logger->output);
    fputs("] ", logger->output);
    fputs(color_start, logger->output);
    fputs(color_end, logger->output);
    fputs(": ", logger->output);
    va_list args;
    va_start(args, format);
    vfprintf(logger->output, format, args);
    va_end(args);
    fputs(color_end, logger->output);
    fputs("\n", logger->output);
    fflush(logger->output);
}

static void handle_error(AionicError error) {
    const char *error_str;
    switch (error.code) {
        case AIONIC_SUCCESS: error_str = "Success"; break;
        case AIONIC_ERROR_CONFIG: error_str = "Configuration Error"; break;
        case AIONIC_ERROR_CACHE: error_str = "Cache Error"; break;
        case AIONIC_ERROR_FIREWALL: error_str = "Firewall Error"; break;
        case AIONIC_ERROR_OPTIMIZER: error_str = "Optimizer Error"; break;
        case AIONIC_ERROR_AI_ROUTER: error_str = "AI Router Error"; break;
        case AIONIC_ERROR_TOKENIZER: error_str = "Tokenizer Error"; break;
        case AIONIC_ERROR_STATS: error_str = "Stats Error"; break;
        case AIONIC_ERROR_PLUGIN: error_str = "Plugin Error"; break;
        case AIONIC_ERROR_SERVER: error_str = "Server Error"; break;
        case AIONIC_ERROR_MEMORY: error_str = "Memory Error"; break;
        case AIONIC_ERROR_SYSTEM: error_str = "System Error"; break;
        default: error_str = "Unknown Error"; break;
    }
    fprintf(stderr, "[ERROR] %s: %s at %s:%d in %s()\n", error_str, error.message, error.file, error.line, error.function);
}

static int initialize_components(AionicSystem *system) {
    if (system->config.enable_tls) {
        tls_global_init();
        logger_log(&system->logger, LOG_LEVEL_INFO, "TLS 1.3 initialized");
    }

    if (system->config.enable_cache && cache_init(system->config.cache_size, system->config.cache_ttl) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CACHE, "Failed to initialize cache")); return -1;
    }
    system->state.cache_initialized = system->config.enable_cache;
    if (system->state.cache_initialized)
        logger_log(&system->logger, LOG_LEVEL_INFO, "Cache initialized (%d entries, %d TTL)", system->config.cache_size, system->config.cache_ttl);

    if (system->config.enable_firewall && firewall_init(&system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_FIREWALL, "Failed to initialize firewall")); return -1;
    }
    system->state.firewall_initialized = system->config.enable_firewall;
    if (system->state.firewall_initialized) logger_log(&system->logger, LOG_LEVEL_INFO, "Firewall initialized");

    if (system->config.enable_optimization && optimizer_init(&system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_OPTIMIZER, "Failed to initialize optimizer")); return -1;
    }
    system->state.optimizer_initialized = system->config.enable_optimization;
    if (system->state.optimizer_initialized) logger_log(&system->logger, LOG_LEVEL_INFO, "Optimizer initialized");

    if (prompt_router_init_with_config(&system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_AI_ROUTER, "Failed to initialize AI prompt router")); return -1;
    }
    system->state.ai_router_initialized = 1;
    logger_log(&system->logger, LOG_LEVEL_INFO, "AI prompt router initialized");

    if (tokenizer_init() != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_TOKENIZER, "Failed to initialize tokenizer")); return -1;
    }
    system->state.tokenizer_initialized = 1;
    logger_log(&system->logger, LOG_LEVEL_INFO, "Tokenizer initialized");

    if (stats_init("stats.json", 300) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_STATS, "Failed to initialize stats collector")); return -1;
    }
    system->state.stats_initialized = 1;
    logger_log(&system->logger, LOG_LEVEL_INFO, "Stats collector initialized");

    if (plugin_init("plugins") != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_PLUGIN, "Failed to initialize plugin system")); return -1;
    }
    system->state.plugin_initialized = 1;
    logger_log(&system->logger, LOG_LEVEL_INFO, "Plugin system initialized");

    if (system->config.enable_observability) {
        obs_init();
        logger_log(&system->logger, LOG_LEVEL_INFO, "Observability system initialized");
    }

    if (system->config.enable_http2) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "HTTP/2 support enabled");
    }

    if (server_init(&system->server, &system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SERVER, "Failed to initialize server")); return -1;
    }
    system->state.server_initialized = 1;

    if (server_start(&system->server) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SERVER, "Failed to start server")); return -1;
    }
    system->state.server_started = 1;

    logger_log(&system->logger, LOG_LEVEL_INFO, "Server started successfully");
    if (system->config.enable_tls) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "AIONIC Server is running on https://localhost:%d and http://localhost:%d",
                   system->config.tls_port, system->config.port);
    } else {
        logger_log(&system->logger, LOG_LEVEL_INFO, "AIONIC Server is running on http://localhost:%d", system->config.port);
    }

    if (system->config.enable_http2) {
        if (system->config.enable_tls) {
            logger_log(&system->logger, LOG_LEVEL_INFO, "HTTP/2 support: h2 (TLS) + h2c (cleartext upgrade)");
        } else {
            logger_log(&system->logger, LOG_LEVEL_INFO, "HTTP/2 support: h2c (cleartext upgrade)");
        }
    }

    return 0;
}

static void cleanup_components(AionicSystem *system) {
    if (system->state.server_started) { server_stop(&system->server); system->state.server_started = 0; }
    if (system->state.server_initialized) { server_cleanup(&system->server); system->state.server_initialized = 0; }
    if (system->state.plugin_initialized) { plugin_cleanup(); system->state.plugin_initialized = 0; }
    if (system->state.stats_initialized) { stats_cleanup(); system->state.stats_initialized = 0; }
    if (system->state.tokenizer_initialized) { tokenizer_cleanup(); system->state.tokenizer_initialized = 0; }
    if (system->state.ai_router_initialized) { prompt_router_cleanup(); system->state.ai_router_initialized = 0; }
    if (system->state.optimizer_initialized) { optimizer_cleanup(); system->state.optimizer_initialized = 0; }
    if (system->state.firewall_initialized) { firewall_cleanup(); system->state.firewall_initialized = 0; }
    if (system->state.cache_initialized) { cache_cleanup(); system->state.cache_initialized = 0; }
    if (system->config.enable_tls) { tls_global_cleanup(); }
    free_config(&system->config);
}

static int thread_pool_init(ThreadPool *pool, int thread_count) {
    pool->thread_count = 0; pool->threads = NULL; (void)thread_count; return 0;
}

static void thread_pool_cleanup(ThreadPool *pool) {
    free(pool->threads); pool->threads = NULL; pool->thread_count = 0;
}

static void drop_privileges(void) {
    if (getuid() == 0) {
        struct passwd *pw = getpwnam("nobody");
        if (pw && (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0))
            handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to drop privileges"));
    }
}

static int config_paths_init(ConfigPaths *paths) {
    paths->config_count = 0; paths->config_capacity = 4;
    paths->config_paths = malloc(sizeof(char *) * (size_t)paths->config_capacity);
    if (!paths->config_paths) { handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_MEMORY, "Failed to allocate memory for config paths")); return -1; }
    return 0;
}

static void config_paths_cleanup(ConfigPaths *paths) {
    if (paths->config_paths) {
        for (int i = 0; i < paths->config_count; i++) free(paths->config_paths[i]);
        free(paths->config_paths); paths->config_paths = NULL;
    }
    paths->config_count = 0; paths->config_capacity = 0;
}

static int safe_file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

static int load_hierarchical_config(const char *base_path, Config *config) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/aionic.conf", base_path);
    if (safe_file_exists(path)) { if (load_config(path, config) != 0) return -1; }
    const char *env = getenv("AIONIC_ENV");
    if (env) {
        snprintf(path, sizeof(path), "%s/%s.conf", base_path, env);
        if (safe_file_exists(path) && load_config(path, config) != 0) return -1;
    }
    snprintf(path, sizeof(path), "%s/local.conf", base_path);
    if (safe_file_exists(path) && load_config(path, config) != 0) return -1;
    return 0;
}

static int setup_inotify(AionicSystem *system, const char *config_path) {
    system->inotify_fd = inotify_init1(IN_NONBLOCK);
    if (system->inotify_fd == -1) return -1;
    system->config_wd = inotify_add_watch(system->inotify_fd, config_path, IN_MODIFY);
    if (system->config_wd == -1) { close(system->inotify_fd); return -1; }
    return 0;
}

static void check_config_reload(AionicSystem *system) {
    if (system->inotify_fd == -1) return;
    char buffer[CONFIG_WATCH_BUFFER_SIZE];
    ssize_t length = read(system->inotify_fd, buffer, sizeof(buffer));
    if (length > 0) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "Configuration file modified, reloading...");
        Config old_config = system->config;
        memset(&system->config, 0, sizeof(Config));
        if (load_hierarchical_config("config", &system->config) != 0) {
            logger_log(&system->logger, LOG_LEVEL_ERROR, "Failed to reload configuration, using previous settings");
            free_config(&system->config);
            system->config = old_config;
        } else {
            logger_log(&system->logger, LOG_LEVEL_INFO, "Configuration reloaded successfully");
        }
    }
}

static void recover_from_error(AionicError error, AionicSystem *system) {
    handle_error(error);
    printf("\n[RECOVERY] Cleaning up after error...\n");
    cleanup_components(system);
    if (system->inotify_fd != -1) { close(system->inotify_fd); system->inotify_fd = -1; }
    config_paths_cleanup(&system->config_paths);
    thread_pool_cleanup(&system->thread_pool);
    printf("[RECOVERY] Completed\n");
}

static time_t last_ocsp_update = 0;

static void handle_ocsp_refresh(AionicSystem *system) {
    if (!system->config.enable_tls || !system->config.tls_enable_ocsp) return;
    time_t now = time(NULL);
    int interval = system->config.tls_ocsp_refresh_interval > 0 ? system->config.tls_ocsp_refresh_interval : 3600;
    if (now - last_ocsp_update >= interval) {
        if (tls_ctx_ocsp_update(system->server.tls_ctx) == 0) {
            logger_log(&system->logger, LOG_LEVEL_INFO, "OCSP response refreshed");
        }
        last_ocsp_update = now;
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    AionicSystem system = {0};
    system.inotify_fd = -1;
    print_version_info();
    logger_init(&system.logger, LOG_LEVEL_INFO, stdout, 1);
    detect_cpu_features();
    setup_signal_handling();

    logger_log(&system.logger, LOG_LEVEL_INFO, "Starting AIONIC Server v%s...", AIONIC_VERSION_STRING);
    logger_log(&system.logger, LOG_LEVEL_INFO, "Hardware acceleration support:");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - AVX2: %s", has_avx2_support() ? "Yes" : "No");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - AVX-512: %s", has_avx512_support() ? "Yes" : "No");

    {
        const char *test_vector = "AIONIC-Hardware-Acceleration-Test";
        size_t len = strlen(test_vector);
        logger_log(&system.logger, LOG_LEVEL_INFO, "Verifying Assembly Linkage...");
        uint32_t crc_val = crc32_asm(test_vector, len);
        logger_log(&system.logger, LOG_LEVEL_INFO, "   [CRC32-SSE4.2] Checksum: 0x%08X", crc_val);
        if (has_avx2_support()) {
            uint32_t crc_avx2 = crc32_asm_avx2(test_vector, len);
            logger_log(&system.logger, LOG_LEVEL_INFO, "   [CRC32-AVX2]   Checksum: 0x%08X", crc_avx2);
            if (crc_val != crc_avx2) logger_log(&system.logger, LOG_LEVEL_WARNING, "   [WARNING] CRC32 mismatch!");
        }
    }

    if (config_paths_init(&system.config_paths) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_MEMORY, "Failed to initialize config paths"), &system);
        return 1;
    }

    if (load_hierarchical_config("config", &system.config) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CONFIG, "Failed to load configuration"), &system);
        return 1;
    }

    logger_log(&system.logger, LOG_LEVEL_INFO, "Configuration loaded successfully");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Port: %d", system.config.port);
    if (system.config.enable_tls) {
        logger_log(&system.logger, LOG_LEVEL_INFO, "   - TLS Port: %d", system.config.tls_port);
    }
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Threads: %d", system.config.thread_count);
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Max Connections: %d", system.config.max_connections);
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - io_uring: %s", system.config.enable_iouring ? "enabled" : "disabled");
    if (system.config.enable_iouring) {
        logger_log(&system.logger, LOG_LEVEL_INFO, "   - io_uring Queue Depth: %d", system.config.iouring_queue_depth);
        logger_log(&system.logger, LOG_LEVEL_INFO, "   - io_uring SQPOLL: %s", system.config.iouring_sqpoll ? "enabled" : "disabled");
    }
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Zero-Copy: %s", system.config.enable_zero_copy ? "enabled" : "disabled");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - TLS: %s", system.config.enable_tls ? "enabled" : "disabled");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - HTTP/2: %s", system.config.enable_http2 ? "enabled" : "disabled");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Smart Routing: %s", system.config.enable_smart_routing ? "enabled" : "disabled");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Streaming: %s", system.config.enable_streaming ? "enabled" : "disabled");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Graceful Shutdown Timeout: %ds", system.config.graceful_shutdown_timeout);

    if (thread_pool_init(&system.thread_pool, system.config.thread_count) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to initialize thread pool"), &system);
        return 1;
    }

    if (setup_inotify(&system, "config/aionic.conf") != 0) {
        logger_log(&system.logger, LOG_LEVEL_WARNING, "Inotify setup failed, hot-reload disabled");
        system.inotify_fd = -1;
    }

    if (initialize_components(&system) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to initialize components"), &system);
        return 1;
    }

    drop_privileges();

    logger_log(&system.logger, LOG_LEVEL_INFO, "Press Ctrl+C to stop the server");
    printf("========================================\n");

    if (system.config.enable_optimization) {
        printf("[OPTIMIZER] Target Pool Size: %d\n", system.config.thread_count * 2);
        printf("[OPTIMIZER] Threads: %d\n", system.config.thread_count);
    }

    if (iouring_available()) {
        printf("[ENGINE] io_uring available - using async I/O\n");
    } else {
        printf("[ENGINE] io_uring not available - using epoll\n");
    }

    if (system.config.enable_zero_copy) {
        printf("[ENGINE] Zero-copy networking enabled\n");
    }

    if (system.config.enable_tls) {
        printf("[TLS] HTTPS on port %d, OCSP: %s\n",
               system.config.tls_port,
               system.config.tls_enable_ocsp ? "enabled" : "disabled");
    }

    if (system.config.enable_http2) {
        if (system.config.enable_tls) {
            printf("[HTTP2] h2 (TLS ALPN) + h2c (cleartext upgrade) enabled\n");
        } else {
            printf("[HTTP2] h2c (cleartext upgrade) enabled\n");
        }
    }

    printf("[SHUTDOWN] Graceful shutdown timeout: %ds\n", system.config.graceful_shutdown_timeout);

    last_ocsp_update = time(NULL);

    while (running && !iouring_global_stop) {
        server_process_events(&system.server);

        if (signal_pipe[0] >= 0) {
            char c;
            ssize_t n = read(signal_pipe[0], &c, 1);
            if (n > 0 && c == 1 && !running) break;
            if (n > 0 && c == 2) {
                printf("[SIGNAL] SIGHUP received - hot reloading API keys...\n");
                prompt_router_hot_reload();
            }
            if (n > 0 && c == 3) {
                printf("[SIGNAL] SIGUSR1 received - refreshing OCSP response...\n");
                if (system.config.tls_enable_ocsp && system.server.tls_ctx) {
                    tls_ctx_ocsp_update(system.server.tls_ctx);
                }
            }
        }

        if (system.config.enable_optimization) optimizer_run(&system.server);
        stats_auto_save();
        check_config_reload(&system);
        handle_ocsp_refresh(&system);
    }

    printf("\n[SHUTDOWN] Shutting down AIONIC Server...\n");
    cleanup_components(&system);
    if (system.inotify_fd != -1) close(system.inotify_fd);
    config_paths_cleanup(&system.config_paths);
    thread_pool_cleanup(&system.thread_pool);
    if (signal_pipe[0] >= 0) close(signal_pipe[0]);
    if (signal_pipe[1] >= 0) close(signal_pipe[1]);

    printf("[SHUTDOWN] AIONIC Server stopped gracefully\n");
    printf("========================================\n");
    return 0;
}
