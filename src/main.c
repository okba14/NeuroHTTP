#define _POSIX_C_SOURCE 200809L

// ===== Standard Library Headers =====
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

// ===== Project Headers =====
#include "config.h"
#include "server.h"
#include "parser.h"
#include "firewall.h"
#include "optimizer.h"
#include "cache.h"
#include "plugin.h"

// ===== AI Modules =====
#include "ai/prompt_router.h"
#include "ai/tokenizer.h"
#include "ai/stats.h"

// ===== Low-level Utils =====
#include "asm_utils.h"

// ===== Version Information =====
#define AIONIC_VERSION_MAJOR 1
#define AIONIC_VERSION_MINOR 0
#define AIONIC_VERSION_PATCH 0
#define AIONIC_VERSION_STRING "1.0.0"

#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif

#ifndef BUILD_TIME
#define BUILD_TIME __TIME__
#endif

#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

// ===== Constants =====
#define CONFIG_WATCH_BUFFER_SIZE 4096

// ===== Error Codes =====
typedef enum {
    AIONIC_SUCCESS = 0,
    AIONIC_ERROR_CONFIG,
    AIONIC_ERROR_CACHE,
    AIONIC_ERROR_FIREWALL,
    AIONIC_ERROR_OPTIMIZER,
    AIONIC_ERROR_AI_ROUTER,
    AIONIC_ERROR_TOKENIZER,
    AIONIC_ERROR_STATS,
    AIONIC_ERROR_PLUGIN,
    AIONIC_ERROR_SERVER,
    AIONIC_ERROR_MEMORY,
    AIONIC_ERROR_SYSTEM
} AionicErrorCode;

// ===== Error Handling =====
typedef struct {
    AionicErrorCode code;
    const char *message;
    const char *file;
    int line;
    const char *function;
} AionicError;

#define AIONIC_ERROR_CREATE(code, message) \
    ((AionicError){code, message, __FILE__, __LINE__, __func__})

// ===== Logging System =====
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

typedef struct {
    LogLevel level;
    FILE *output;
    int use_colors;
} Logger;

// ===== System State =====
typedef struct {
    int cache_initialized;
    int firewall_initialized;
    int optimizer_initialized;
    int ai_router_initialized;
    int tokenizer_initialized;
    int stats_initialized;
    int plugin_initialized;
    int server_initialized;
    int server_started;
} SystemState;

// ===== Thread Pool =====
typedef struct {
    pthread_t *threads;
    int thread_count;
} ThreadPool;

// ===== Config Paths =====
typedef struct {
    char **config_paths;
    int config_count;
    int config_capacity;
} ConfigPaths;

// ===== AIONIC System =====
typedef struct {
    Config config;
    Server server;
    Logger logger;
    SystemState state;
    ThreadPool thread_pool;
    ConfigPaths config_paths;
    int inotify_fd;
    int config_wd;
} AionicSystem;

// Global variable to control server running state
volatile sig_atomic_t running = 1;

// Function prototypes
static void handle_signal(int sig);
static void print_version_info(void);
static void logger_init(Logger *logger, LogLevel level, FILE *output, int use_colors);
static void logger_log(Logger *logger, LogLevel level, const char *format, ...) 
    __attribute__((format(printf, 3, 4)));
static void handle_error(AionicError error);
static int initialize_components(AionicSystem *system);
static void cleanup_components(AionicSystem *system);
static int thread_pool_init(ThreadPool *pool, int thread_count);
static void thread_pool_cleanup(ThreadPool *pool);
static void *worker_thread(void *arg);
static void drop_privileges(void);
static int config_paths_init(ConfigPaths *paths);
static void config_paths_cleanup(ConfigPaths *paths);
static int load_hierarchical_config(const char *base_path, Config *config);
static int setup_inotify(AionicSystem *system, const char *config_path);
static void check_config_reload(AionicSystem *system);
static void recover_from_error(AionicError error, AionicSystem *system);
static int safe_file_exists(const char *path);

/**
 * @brief Signal handler for graceful shutdown
 * 
 * This function is called when a signal is received to shut down
 * the server gracefully.
 * 
 * @param sig The signal number
 */
static void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    running = 0;
}

/**
 * @brief Print version and build information
 */
static void print_version_info(void) {
    printf("========================================\n");
    printf("    AIONIC AI Web Server v%s\n", AIONIC_VERSION_STRING);
    printf("========================================\n");
    printf("Build: %s %s\n", BUILD_DATE, BUILD_TIME);
    printf("Git commit: %s\n", GIT_COMMIT);
    printf("========================================\n");
}

/**
 * @brief Initialize the logging system
 * 
 * @param logger Pointer to the logger structure
 * @param level Minimum log level to display
 * @param output Output file stream
 * @param use_colors Whether to use colored output
 */
static void logger_init(Logger *logger, LogLevel level, FILE *output, int use_colors) {
    logger->level = level;
    logger->output = output;
    logger->use_colors = use_colors;
}

/**
 * @brief Log a message with the specified level
 * 
 * @param logger Pointer to the logger structure
 * @param level Log level
 * @param format Format string (must be a literal string)
 * @param ... Variable arguments
 */
static void logger_log(Logger *logger, LogLevel level, const char *format, ...) {
    if (level < logger->level) return;
    
    const char *level_str;
    const char *color_start = "";
    const char *color_end = "";
    
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
    
    // Print timestamp and message
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Use fputs for constant strings to avoid format string issues
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

/**
 * @brief Handle an error by printing its details
 * 
 * @param error The error structure
 */
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
    
    // Use fputs for constant strings to avoid format string issues
    fputs("❌ ", stderr);
    fputs(error_str, stderr);
    fputs(": ", stderr);
    fputs(error.message, stderr);
    fputs("\n   at ", stderr);
    fputs(error.file, stderr);
    fputs(":", stderr);
    fprintf(stderr, "%d", error.line);
    fputs(" in ", stderr);
    fputs(error.function, stderr);
    fputs("()\n", stderr);
}

/**
 * @brief Initialize all system components
 * 
 * @param system Pointer to the AIONIC system structure
 * @return 0 on success, -1 on failure
 */
static int initialize_components(AionicSystem *system) {
    // Initialize cache
    if (system->config.enable_cache && cache_init(system->config.cache_size, system->config.cache_ttl) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CACHE, "Failed to initialize cache"));
        return -1;
    }
    system->state.cache_initialized = system->config.enable_cache;
    
    if (system->state.cache_initialized) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "Cache initialized (%d entries, %d TTL)", 
                   system->config.cache_size, system->config.cache_ttl);
    }
    
    // Initialize firewall
    if (system->config.enable_firewall && firewall_init(&system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_FIREWALL, "Failed to initialize firewall"));
        return -1;
    }
    system->state.firewall_initialized = system->config.enable_firewall;
    
    if (system->state.firewall_initialized) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "Firewall initialized");
    }
    
    // Initialize optimizer
    if (system->config.enable_optimization && optimizer_init(&system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_OPTIMIZER, "Failed to initialize optimizer"));
        return -1;
    }
    system->state.optimizer_initialized = system->config.enable_optimization;
    
    if (system->state.optimizer_initialized) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "Optimizer initialized");
    }
    
    // Initialize AI prompt router
    if (prompt_router_init() != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_AI_ROUTER, "Failed to initialize AI prompt router"));
        return -1;
    }
    system->state.ai_router_initialized = 1;
    
    logger_log(&system->logger, LOG_LEVEL_INFO, "AI prompt router initialized");
    
    // Initialize tokenizer
    if (tokenizer_init() != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_TOKENIZER, "Failed to initialize tokenizer"));
        return -1;
    }
    system->state.tokenizer_initialized = 1;
    
    logger_log(&system->logger, LOG_LEVEL_INFO, "Tokenizer initialized");
    
    // Initialize stats collector
    if (stats_init("stats.json", 300) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_STATS, "Failed to initialize stats collector"));
        return -1;
    }
    system->state.stats_initialized = 1;
    
    logger_log(&system->logger, LOG_LEVEL_INFO, "Stats collector initialized");
    
    // Initialize plugin system
    if (plugin_init("plugins") != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_PLUGIN, "Failed to initialize plugin system"));
        return -1;
    }
    system->state.plugin_initialized = 1;
    
    logger_log(&system->logger, LOG_LEVEL_INFO, "Plugin system initialized");
    
    // Create and start server
    if (server_init(&system->server, &system->config) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SERVER, "Failed to initialize server"));
        return -1;
    }
    system->state.server_initialized = 1;
    
    if (server_start(&system->server) != 0) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SERVER, "Failed to start server"));
        return -1;
    }
    system->state.server_started = 1;
    
    logger_log(&system->logger, LOG_LEVEL_INFO, "Server started successfully");
    logger_log(&system->logger, LOG_LEVEL_INFO, "AIONIC Server is running on http://localhost:%d", 
               system->config.port);
    
    return 0;
}

/**
 * @brief Clean up all system components
 * 
 * @param system Pointer to the AIONIC system structure
 */
static void cleanup_components(AionicSystem *system) {
    if (system->state.server_started) {
        server_stop(&system->server);
        system->state.server_started = 0;
    }
    
    if (system->state.server_initialized) {
        server_cleanup(&system->server);
        system->state.server_initialized = 0;
    }
    
    if (system->state.plugin_initialized) {
        plugin_cleanup();
        system->state.plugin_initialized = 0;
    }
    
    if (system->state.stats_initialized) {
        stats_cleanup();
        system->state.stats_initialized = 0;
    }
    
    if (system->state.tokenizer_initialized) {
        tokenizer_cleanup();
        system->state.tokenizer_initialized = 0;
    }
    
    if (system->state.ai_router_initialized) {
        prompt_router_cleanup();
        system->state.ai_router_initialized = 0;
    }
    
    if (system->state.optimizer_initialized) {
        optimizer_cleanup();
        system->state.optimizer_initialized = 0;
    }
    
    if (system->state.firewall_initialized) {
        firewall_cleanup();
        system->state.firewall_initialized = 0;
    }
    
    if (system->state.cache_initialized) {
        cache_cleanup();
        system->state.cache_initialized = 0;
    }
    
    free_config(&system->config);
}

/**
 * @brief Initialize thread pool
 * 
 * @param pool Pointer to the thread pool structure
 * @param thread_count Number of threads to create
 * @return 0 on success, -1 on failure
 */
static int thread_pool_init(ThreadPool *pool, int thread_count) {
    pool->thread_count = thread_count;
    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    if (!pool->threads) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_MEMORY, "Failed to allocate memory for thread pool"));
        return -1;
    }
    
    // Create threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, NULL) != 0) {
            handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to create worker thread"));
            free(pool->threads);
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief Clean up thread pool
 * 
 * @param pool Pointer to the thread pool structure
 */
static void thread_pool_cleanup(ThreadPool *pool) {
    if (pool->threads) {
        // Cancel threads
        for (int i = 0; i < pool->thread_count; i++) {
            pthread_cancel(pool->threads[i]);
        }
        
        // Join threads
        for (int i = 0; i < pool->thread_count; i++) {
            pthread_join(pool->threads[i], NULL);
        }
        
        free(pool->threads);
        pool->threads = NULL;
    }
}

/**
 * @brief Worker thread function
 * 
 * @param arg Thread argument (not used)
 * @return NULL
 */
static void *worker_thread(void *arg) {
    (void)arg;  // Avoid unused parameter warning
    
    // Set thread cancellation state
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    // Worker thread main loop
    while (running) {
        
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10000000; // 10ms
        nanosleep(&ts, NULL);
    }
    
    return NULL;
}

/**
 * @brief Drop privileges to 'nobody' user if running as root
 */
static void drop_privileges(void) {
    if (getuid() == 0) {
        struct passwd *pw = getpwnam("nobody");
        if (pw) {
            if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
                handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to drop privileges"));
            } else {
                printf("✅ Dropped privileges to user 'nobody'\n");
            }
        }
    }
}

/**
 * @brief Initialize config paths structure
 * 
 * @param paths Pointer to the config paths structure
 * @return 0 on success, -1 on failure
 */
static int config_paths_init(ConfigPaths *paths) {
    paths->config_count = 0;
    paths->config_capacity = 4;
    paths->config_paths = malloc(sizeof(char *) * paths->config_capacity);
    if (!paths->config_paths) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_MEMORY, 
                                        "Failed to allocate memory for config paths"));
        return -1;
    }
    
    return 0;
}

/**
 * @brief Clean up config paths structure
 * 
 * @param paths Pointer to the config paths structure
 */
static void config_paths_cleanup(ConfigPaths *paths) {
    if (paths->config_paths) {
        for (int i = 0; i < paths->config_count; i++) {
            free(paths->config_paths[i]);
        }
        free(paths->config_paths);
        paths->config_paths = NULL;
    }
    paths->config_count = 0;
    paths->config_capacity = 0;
}

/**
 * @brief Safely check if a file exists
 * 
 * This function avoids the TOCTOU (Time-of-Check to Time-of-Use) vulnerability
 * by not using access() and instead using stat().
 * 
 * @param path Path to the file
 * @return 1 if file exists, 0 if it doesn't, -1 on error
 */
static int safe_file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISREG(st.st_mode) ? 1 : 0;
    }
    return 0;
}

/**
 * @brief Load hierarchical configuration files
 * 
 * @param base_path Base configuration directory path
 * @param config Pointer to the configuration structure
 * @return 0 on success, -1 on failure
 */
static int load_hierarchical_config(const char *base_path, Config *config) {
    char path[PATH_MAX];
    

    snprintf(path, sizeof(path), "%s/base.conf", base_path);
    if (safe_file_exists(path)) {
        if (load_config(path, config) != 0) {
            handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CONFIG, "Failed to load base configuration"));
            return -1;
        }
    } else {

        snprintf(path, sizeof(path), "%s/aionic.conf", base_path);
        if (load_config(path, config) != 0) {
            handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CONFIG, "Failed to load configuration"));
            return -1;
        }
    }
    

    const char *env = getenv("AIONIC_ENV");
    if (env) {
        snprintf(path, sizeof(path), "%s/%s.conf", base_path, env);
        if (safe_file_exists(path) && load_config(path, config) != 0) {
            handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CONFIG, 
                                            "Failed to load environment configuration"));
            return -1;
        }
    }
    

    snprintf(path, sizeof(path), "%s/local.conf", base_path);
    if (safe_file_exists(path)) {
        if (load_config(path, config) != 0) {
            handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CONFIG, "Failed to load local configuration"));
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief Set up inotify to watch for configuration file changes
 * 
 * @param system Pointer to the AIONIC system structure
 * @param config_path Path to the configuration file
 * @return 0 on success, -1 on failure
 */
static int setup_inotify(AionicSystem *system, const char *config_path) {
    system->inotify_fd = inotify_init1(IN_NONBLOCK);
    if (system->inotify_fd == -1) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to initialize inotify"));
        return -1;
    }
    
    system->config_wd = inotify_add_watch(system->inotify_fd, config_path, IN_MODIFY);
    if (system->config_wd == -1) {
        handle_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to add config file to inotify"));
        close(system->inotify_fd);
        return -1;
    }
    
    logger_log(&system->logger, LOG_LEVEL_DEBUG, "Inotify set up for config file: %s", config_path);
    return 0;
}

/**
 * @brief Check for configuration file changes and reload if necessary
 * 
 * @param system Pointer to the AIONIC system structure
 */
static void check_config_reload(AionicSystem *system) {
    if (system->inotify_fd == -1) return;
    
    char buffer[CONFIG_WATCH_BUFFER_SIZE];
    ssize_t length = read(system->inotify_fd, buffer, sizeof(buffer));
    
    if (length > 0) {
        logger_log(&system->logger, LOG_LEVEL_INFO, "Configuration file modified, reloading...");
        

        Config old_config = system->config;
        

        if (load_hierarchical_config("config", &system->config) != 0) {
            logger_log(&system->logger, LOG_LEVEL_ERROR, 
                       "Failed to reload configuration, using previous settings");
            system->config = old_config;
        } else {
            logger_log(&system->logger, LOG_LEVEL_INFO, "Configuration reloaded successfully");
            
            
        }
    }
}

/**
 * @brief Recover from an error by cleaning up resources
 * 
 * @param error The error that occurred
 * @param system Pointer to the AIONIC system structure
 */
static void recover_from_error(AionicError error, AionicSystem *system) {
    handle_error(error);
    
    printf("\n🛑 Recovering from error...\n");
    
    cleanup_components(system);
    
    // Clean up additional resources
    if (system->inotify_fd != -1) {
        close(system->inotify_fd);
        system->inotify_fd = -1;
    }
    
    config_paths_cleanup(&system->config_paths);
    thread_pool_cleanup(&system->thread_pool);
    
    printf("✅ Recovery completed\n");
}

/**
 * @brief Main entry point for the AIONIC AI Web Server
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code
 */
int main(int argc, char *argv[]) {
    (void)argc;  // Avoid unused parameter warning
    (void)argv;  // Avoid unused parameter warning
    
    // Initialize AIONIC system
    AionicSystem system = {0};
    system.inotify_fd = -1;
    
    // Print version information
    print_version_info();
    
    // Initialize logger
    logger_init(&system.logger, LOG_LEVEL_INFO, stdout, 1);
    
    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    logger_log(&system.logger, LOG_LEVEL_INFO, "Starting AIONIC Server...");
    
    // Display hardware acceleration support
    logger_log(&system.logger, LOG_LEVEL_INFO, "Hardware acceleration support:");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - AVX2: %s", has_avx2_support() ? "Yes" : "No");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - AVX-512: %s", has_avx512_support() ? "Yes" : "No");
    
    // Initialize config paths
    if (config_paths_init(&system.config_paths) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_MEMORY, "Failed to initialize config paths"), 
                          &system);
        return 1;
    }
    
    // Load hierarchical configuration
    if (load_hierarchical_config("config", &system.config) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_CONFIG, "Failed to load configuration"), 
                          &system);
        return 1;
    }
    
    logger_log(&system.logger, LOG_LEVEL_INFO, "Configuration loaded successfully");
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Port: %d", system.config.port);
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Threads: %d", system.config.thread_count);
    logger_log(&system.logger, LOG_LEVEL_INFO, "   - Max Connections: %d", system.config.max_connections);
    
    // Initialize thread pool
    if (thread_pool_init(&system.thread_pool, system.config.thread_count) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to initialize thread pool"), 
                          &system);
        return 1;
    }
    
    // Set up inotify for configuration hot reload
    if (setup_inotify(&system, "config/aionic.conf") != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to set up inotify"), 
                          &system);
        return 1;
    }
    
    // Initialize all components
    if (initialize_components(&system) != 0) {
        recover_from_error(AIONIC_ERROR_CREATE(AIONIC_ERROR_SYSTEM, "Failed to initialize components"), 
                          &system);
        return 1;
    }
    
    // Drop privileges if running as root
    drop_privileges();
    
    logger_log(&system.logger, LOG_LEVEL_INFO, "Press Ctrl+C to stop the server");
    printf("========================================\n");
    
    // Main server loop
    while (running) {
        // Use original server event processing
        server_process_events(&system.server);
        
        if (system.config.enable_optimization) {
            optimizer_run(&system.server);
        }
        
        stats_auto_save();
        
        // Check for configuration changes
        check_config_reload(&system);
        
        // Use nanosleep instead of usleep
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000; // 100ms = 100,000,000 nanoseconds
        nanosleep(&ts, NULL);
    }
    
    printf("\n🛑 Shutting down AIONIC Server...\n");
    
    // Clean up resources on exit
    cleanup_components(&system);
    
    if (system.inotify_fd != -1) {
        close(system.inotify_fd);
    }
    
    config_paths_cleanup(&system.config_paths);
    thread_pool_cleanup(&system.thread_pool);
    
    printf("✅ AIONIC Server stopped gracefully\n");
    printf("========================================\n");
    
    return 0;
}
