#define _POSIX_C_SOURCE 200809L

#define MAX_EVENTS 1024   
#define BACKLOG 128       

// ===== Configuration Constants =====
#define KEEP_ALIVE_TIMEOUT 30     // Close connections idle for 30 seconds
#define CLEANUP_INTERVAL 5        // Check for idle connections every 5 seconds

// ===== Standard Library Headers =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>  

// ===== Project Headers =====
#include "server.h"
#include "parser.h"
#include "router.h"
#include "stream.h"
#include "utils.h"
#include "asm_utils.h"
#include "firewall.h"
#include "config.h"

// Thread data structure
typedef struct {
    Server *server;
    int epoll_fd;
    int id;
    FirewallStats firewall_stats;  
} ThreadData;

// Connection tracking structure (UPDATED for Keep-Alive)
typedef struct {
    int client_fd;
    int epoll_owner_id; 
    char ip_address[INET_ADDRSTRLEN];
    time_t connection_time;
    time_t last_activity; 
    uint64_t bytes_received;
    uint64_t bytes_sent;
    int requests_handled;
    int flagged_suspicious;
} ConnectionInfo;

// Global connection tracking
static ConnectionInfo *connections = NULL;
static int connection_count = 0;
static int connection_capacity = 0;
static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===== Helper Functions =====
static ConnectionInfo *add_connection_info(int client_fd, const char *ip_address, int thread_id) {
    pthread_mutex_lock(&connection_mutex);
    
    if (connection_count >= connection_capacity) {
        int new_capacity = connection_capacity == 0 ? 128 : connection_capacity * 2;
        ConnectionInfo *new_connections = realloc(connections, sizeof(ConnectionInfo) * new_capacity);
        if (!new_connections) {
            pthread_mutex_unlock(&connection_mutex);
            return NULL;
        }
        
        connections = new_connections;
        connection_capacity = new_capacity;
    }
    
    time_t now = time(NULL);
    ConnectionInfo *info = &connections[connection_count];
    info->client_fd = client_fd;
    info->epoll_owner_id = thread_id;
    strncpy(info->ip_address, ip_address, INET_ADDRSTRLEN - 1);
    info->ip_address[INET_ADDRSTRLEN - 1] = '\0';
    info->connection_time = now;
    info->last_activity = now; 
    info->bytes_received = 0;
    info->bytes_sent = 0;
    info->requests_handled = 0;
    info->flagged_suspicious = 0;
    
    connection_count++;
    pthread_mutex_unlock(&connection_mutex);
    return info;
}

static ConnectionInfo *find_connection_info(int client_fd) {
    pthread_mutex_lock(&connection_mutex);
    
    for (int i = 0; i < connection_count; i++) {
        if (connections[i].client_fd == client_fd) {
            pthread_mutex_unlock(&connection_mutex);
            return &connections[i];
        }
    }
    
    pthread_mutex_unlock(&connection_mutex);
    return NULL;
}

static void remove_connection_info(int client_fd) {
    pthread_mutex_lock(&connection_mutex);
    
    for (int i = 0; i < connection_count; i++) {
        if (connections[i].client_fd == client_fd) {
            // Move last element to current position
            if (i < connection_count - 1) {
                connections[i] = connections[connection_count - 1];
            }
            connection_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&connection_mutex);
}

static void log_connection_info(ConnectionInfo *info, const char *event) {
    char log_msg[512];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    snprintf(log_msg, sizeof(log_msg), "[%s] Connection %s: FD=%d, IP=%s, Requests=%d, BytesIn=%lu, BytesOut=%lu, Suspicious=%d",
             time_buffer, event, info->client_fd, info->ip_address, info->requests_handled, 
             info->bytes_received, info->bytes_sent, info->flagged_suspicious);
    
    log_message("SERVER", log_msg);
}

static int extract_api_key(const HTTPRequest *request) {
    // Check for API key in headers
    for (int i = 0; i < request->header_count; i++) {
        if (request->headers[i]) {
            // Check if this is an Authorization header
            if (strncmp(request->headers[i], "Authorization:", 13) == 0) {
                // Extract the value part (after "Authorization:")
                char *value = request->headers[i] + 13;
                // Skip whitespace
                while (*value && isspace(*value)) value++;
                
                // Format: "Bearer <key>" or "ApiKey <key>"
                if (strstr(value, "Bearer ") == value) {
                    return 1;  
                } else if (strstr(value, "ApiKey ") == value) {
                    return 1;  
                }
            }
        }
    }
    
    // Check for API key in query parameters
    if (request->query_string) {
        if (strstr(request->query_string, "api_key=") || 
            strstr(request->query_string, "apikey=")) {
            return 1;
        }
    }
    
    return 0;  // No API key found
}

static char *get_header_value(const HTTPRequest *request, const char *header_name) {
    size_t name_len = strlen(header_name);
    
    for (int i = 0; i < request->header_count; i++) {
        if (request->headers[i]) {
            // Check if this header starts with header name followed by colon
            if (strncmp(request->headers[i], header_name, name_len) == 0 && 
                request->headers[i][name_len] == ':') {
                // Extract the value part (after "header_name:")
                char *value = request->headers[i] + name_len + 1;
                // Skip whitespace
                while (*value && isspace(*value)) value++;
                return value;
            }
        }
    }
    
    return NULL;
}

static int is_suspicious_user_agent(const char *user_agent) {
    if (!user_agent) return 0;
    
    // Check for known malicious user agents
    if (strstr(user_agent, "sqlmap") || 
        strstr(user_agent, "nikto") ||
        strstr(user_agent, "nmap") ||
        strstr(user_agent, "w3af") ||
        strstr(user_agent, "burp") ||
        strstr(user_agent, "metasploit")) {
        return 1;
    }
    
    return 0;
}

static int is_suspicious_request(const HTTPRequest *request) {
    // Check for suspicious patterns in request
    if (request->path) {
        // Check for path traversal attempts
        if (strstr(request->path, "../") || strstr(request->path, "..\\")) {
            return 1;
        }
        
        // Check for suspicious file extensions in paths that shouldn't have them
        if (strstr(request->path, ".php") || strstr(request->path, ".asp") || 
            strstr(request->path, ".jsp") || strstr(request->path, ".exe")) {
            // Only suspicious if not in a legitimate path
            if (!strstr(request->path, "/api/") && !strstr(request->path, "/static/")) {
                return 1;
            }
        }
    }
    
    // Check for suspicious user agent
    char *user_agent = get_header_value(request, "User-Agent");
    if (is_suspicious_user_agent(user_agent)) {
        return 1;
    }
    
    // Check for unusually large payloads
    if (request->body_length > 10000000) {  // 10MB
        return 1;
    }
    
    // Check for suspicious content types with large payloads
    if (request->content_type && request->body_length > 1000000) {  // 1MB
        if (strstr(request->content_type, "application/x-www-form-urlencoded") ||
            strstr(request->content_type, "multipart/form-data")) {
            // Check for SQL injection patterns in form data
            if (request->body && 
                (strstr(request->body, " UNION ") || 
                 strstr(request->body, " OR ") ||
                 strstr(request->body, " AND "))) {
                return 1;
            }
        }
    }
    
    return 0;
}

// More intelligent attack pattern detection
static int contains_attack_pattern(const char *data, const char *pattern) {
    if (!data || !pattern) return 0;
    
    // Find pattern in data
    char *found = strstr(data, pattern);
    if (!found) return 0;
    
    // Check if it's in a context that makes it suspicious
    size_t pattern_len = strlen(pattern);
    size_t data_len = strlen(data);
    size_t found_pos = found - data;
    
    // Check character before and after pattern
    char before = found_pos > 0 ? data[found_pos - 1] : ' ';
    char after = found_pos + pattern_len < data_len ? data[found_pos + pattern_len] : ' ';
    
    // SQL keywords are suspicious when surrounded by non-alphanumeric characters
    if (strstr(pattern, "SELECT") || strstr(pattern, "INSERT") || 
        strstr(pattern, "UPDATE") || strstr(pattern, "DELETE") ||
        strstr(pattern, "UNION") || strstr(pattern, "DROP")) {
        // Check if it's actually part of a SQL query
        if ((!isalnum(before) && before != ' ') && (!isalnum(after) && after != ' ')) {
            return 1;
        }
    }
    
    // XSS patterns are suspicious in certain contexts
    if (strstr(pattern, "<script") || strstr(pattern, "javascript:")) {
        // Check if it's in an HTML context
        if (strstr(data, "<") || strstr(data, "onload=") || strstr(data, "onerror=")) {
            return 1;
        }
    }
    
    return 0;
}

// ===== Reaper Thread for Idle Connections (NEW) =====
static void *reaper_thread(void *arg) {
    Server *server = (Server *)arg;
    printf("[REAPER] Idle connection monitor started (Timeout: %ds)\n", KEEP_ALIVE_TIMEOUT);

    while (server->running) {
        sleep(CLEANUP_INTERVAL);
        
        pthread_mutex_lock(&connection_mutex);
        time_t now = time(NULL);
        
        // Iterate backwards to safely remove elements
        for (int i = connection_count - 1; i >= 0; i--) {
            ConnectionInfo *info = &connections[i];
            
            if (now - info->last_activity > KEEP_ALIVE_TIMEOUT) {
                printf("[REAPER] Closing idle connection: FD=%d, IP=%s (Idle: %lds)\n", 
                       info->client_fd, info->ip_address, now - info->last_activity);
                
                // Remove from epoll 
                if (info->epoll_owner_id >= 0 && info->epoll_owner_id < server->thread_count) {
                    int target_epoll = server->epoll_fds[info->epoll_owner_id];
                    epoll_ctl(target_epoll, EPOLL_CTL_DEL, info->client_fd, NULL);
                }
                
                close(info->client_fd);
                server->active_connections--; 
                
                // Remove from array (swap with last)
                if (i < connection_count - 1) {
                    connections[i] = connections[connection_count - 1];
                }
                connection_count--;
                
                i++; 
            }
        }
        
        pthread_mutex_unlock(&connection_mutex);
    }
    
    printf("[REAPER] Idle connection monitor stopped\n");
    return NULL;
}

// ===== Worker Thread Function =====
static void *worker_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    Server *server = data->server;
    
    printf("Worker thread %d started\n", data->id);
    
    struct epoll_event events[MAX_EVENTS];
    
    while (server->running) {
        // Wait for events
        int event_count = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 100);
        
        if (event_count < 0) {
            if (errno == EINTR) continue;  
            perror("epoll_wait");
            break;
        }
        
        // Process events
        for (int i = 0; i < event_count; i++) {
            int client_fd = events[i].data.fd;
            
            if (events[i].events & EPOLLIN) {
                // Data ready to read
                if (server_handle_request(server, client_fd) != 0) {
                    // Error handling request, close connection
                    epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    
                    // Critical Section: Update active connections safely
                    pthread_mutex_lock(&connection_mutex);
                    server->active_connections--;
                    // Remove from tracking
                    for (int j = 0; j < connection_count; j++) {
                        if (connections[j].client_fd == client_fd) {
                             // Swap and pop
                             if (j < connection_count - 1) connections[j] = connections[connection_count - 1];
                             connection_count--;
                             break;
                        }
                    }
                    pthread_mutex_unlock(&connection_mutex);
                }
            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                // Connection error or closed
                epoll_ctl(data->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                close(client_fd);
                
                pthread_mutex_lock(&connection_mutex);
                server->active_connections--;
                 // Remove from tracking
                for (int j = 0; j < connection_count; j++) {
                    if (connections[j].client_fd == client_fd) {
                         // Swap and pop
                         if (j < connection_count - 1) connections[j] = connections[connection_count - 1];
                         connection_count--;
                         break;
                    }
                }
                pthread_mutex_unlock(&connection_mutex);
            }
        }
    }
    
    printf("Worker thread %d exiting\n", data->id);
    free(data);
    return NULL;
}

// ===== Server Functions =====
int server_init(Server *server, const Config *config) {
    memset(server, 0, sizeof(Server));
    
    server->port = config->port;
    server->thread_count = config->thread_count;
    server->max_connections = config->max_connections;
    server->running = 0; 
    
    // Initialize statistics
    server->stats.total_requests = 0;
    server->stats.total_responses = 0;
    server->stats.bytes_sent = 0;
    server->stats.bytes_received = 0; 
    server->stats.avg_response_time = 0.0;
    
    // Create server socket
    server->server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->server_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server->server_fd);
        return -1;
    }
    
    // Bind socket to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);
    
    if (bind(server->server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server->server_fd);
        return -1;
    }
    
    // Start listening
    if (listen(server->server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server->server_fd);
        return -1;
    }
    
    // Initialize thread pool
    server->thread_pool = malloc(sizeof(pthread_t) * server->thread_count);
    if (!server->thread_pool) {
        perror("malloc");
        close(server->server_fd);
        return -1;
    }
    
    // Initialize request queue
    server->request_queue = malloc(sizeof(void *) * server->max_connections);
    if (!server->request_queue) {
        perror("malloc");
        free(server->thread_pool);
        close(server->server_fd);
        return -1;
    }
    
    // Initialize epoll fds array
    server->epoll_fds = malloc(sizeof(int) * server->thread_count);
    if (!server->epoll_fds) {
        perror("malloc");
        free(server->request_queue);
        free(server->thread_pool);
        close(server->server_fd);
        return -1;
    }
    
    // Initialize routes
    init_routes();
    
    // Initialize firewall with enhanced features
    if (firewall_init(config) != 0) {
        fprintf(stderr, "Failed to initialize firewall\n");
        // Continue without firewall for now
    }
    
    // Add only high-severity attack patterns to firewall
    firewall_add_attack_pattern("<script", 9);
    firewall_add_attack_pattern("javascript:", 8);
    firewall_add_attack_pattern("onload=", 8);
    firewall_add_attack_pattern("onerror=", 8);
    firewall_add_attack_pattern("alert(", 8);
    firewall_add_attack_pattern("document.cookie", 8);
    firewall_add_attack_pattern("eval(", 9);
    firewall_add_attack_pattern("iframe", 7);
    
    return 0;
}

int server_start(Server *server) {
    server->running = 1; // Set running flag to true
    
    // Create worker threads
    for (int i = 0; i < server->thread_count; i++) {
        ThreadData *data = malloc(sizeof(ThreadData));
        if (!data) {
            perror("malloc");
            continue;
        }
        
        data->server = server;
        data->id = i;
        memset(&data->firewall_stats, 0, sizeof(FirewallStats));
        
        // Create epoll instance for each thread
        data->epoll_fd = epoll_create1(0);
        if (data->epoll_fd < 0) {
            perror("epoll_create1");
            free(data);
            continue;
        }
        
        // Store epoll fd in server structure
        server->epoll_fds[i] = data->epoll_fd;
        
        if (pthread_create(&((pthread_t *)server->thread_pool)[i], NULL, worker_thread, data) != 0) {
            perror("pthread_create");
            close(data->epoll_fd);
            free(data);
            continue;
        }
    }

    // Start Reaper Thread (Keep-Alive Monitor)
    if (pthread_create(&server->reaper_thread, NULL, reaper_thread, server) != 0) {
        perror("pthread_create reaper");
    }
    
    return 0;
}

int server_stop(Server *server) {
    printf("Stopping server...\n");
    
    // Set running flag to false to signal threads to stop
    server->running = 0;
    
    // Join Reaper Thread
    pthread_join(server->reaper_thread, NULL);

    // Wait for worker threads to finish
    for (int i = 0; i < server->thread_count; i++) {
        pthread_join(((pthread_t *)server->thread_pool)[i], NULL);
        
        // Close epoll fd
        if (server->epoll_fds[i] >= 0) {
            close(server->epoll_fds[i]);
        }
    }
    
    // Clean up firewall
    firewall_cleanup();
    
    // Clean up connection tracking
    pthread_mutex_lock(&connection_mutex);
    free(connections);
    connections = NULL;
    connection_count = 0;
    connection_capacity = 0;
    pthread_mutex_unlock(&connection_mutex);
    
    printf("Server stopped\n");
    return 0;
}

void server_cleanup(Server *server) {
    if (server->server_fd >= 0) {
        close(server->server_fd);
    }
    
    if (server->thread_pool) {
        free(server->thread_pool);
    }
    
    if (server->request_queue) {
        free(server->request_queue);
    }
    
    if (server->epoll_fds) {
        free(server->epoll_fds);
    }
}

int server_process_events(Server *server) {
    // Accept new connections
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (server->active_connections < server->max_connections) {
        int client_fd = accept(server->server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more new connections
                break;
            }
            perror("accept");
            return -1;
        }
        
        // Set socket to non-blocking
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        
        // Get client IP address
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        
        // Check firewall before accepting connection
        if (firewall_is_blacklisted(client_ip)) {
            printf("Connection rejected - IP blacklisted: %s\n", client_ip);
            close(client_fd);
            continue;
        }
        
        // Distribute connection to one of threads
        int thread_id = server->active_connections % server->thread_count;
        
        // Add connection tracking (pass thread_id)
        ConnectionInfo *info = add_connection_info(client_fd, client_ip, thread_id);
        if (!info) {
            printf("Failed to track connection - rejecting: %s\n", client_ip);
            close(client_fd);
            continue;
        }
        
        int epoll_fd = server->epoll_fds[thread_id];
        
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;  
        event.data.fd = client_fd;
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
            perror("epoll_ctl");
            close(client_fd);
            remove_connection_info(client_fd);
            return -1;
        }
        
        server->active_connections++;
        
        printf("New connection from %s:%d (fd: %d)\n", client_ip, ntohs(client_addr.sin_port), client_fd);
        
        // Log new connection
        log_connection_info(info, "established");
    }
    
    return 0;
}

int server_handle_request(Server *server, int client_fd) {
    char buffer[8192];  
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        // Connection closed by client or error
        return -1;  
    }
    
    buffer[bytes_read] = '\0';  
    
    // Track bytes received in server stats
    server->stats.bytes_received += bytes_read;
    
    // Get connection info
    ConnectionInfo *info = find_connection_info(client_fd);
    if (!info) {
        return -1; 
    }

    // Update activity timestamp (Keep-Alive reset)
    info->bytes_received += bytes_read;
    info->requests_handled++;
    info->last_activity = time(NULL); 
    
    // Parse request
    HTTPRequest request;
    if (parse_http_request(buffer, &request) != 0) {
        // Check for firewall attack patterns in raw request - only high severity patterns
        if (contains_attack_pattern(buffer, "<script") || 
            contains_attack_pattern(buffer, "javascript:") ||
            contains_attack_pattern(buffer, "eval(")) {
            printf("Connection blocked by firewall - attack pattern detected\n");
            info->flagged_suspicious = 1;
            log_connection_info(info, "blocked");
            return -1;
        }
        
        // Error parsing request
        const char *error_response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(client_fd, error_response, strlen(error_response), 0);
        return -1;
    }
    
    // Extract API key for potential future use
    (void)extract_api_key(&request);  
    
    // Check firewall with basic detection - only for blacklisted IPs
    if (firewall_is_blacklisted(info->ip_address)) {
        printf("Connection blocked by firewall - IP blacklisted\n");
        info->flagged_suspicious = 1;
        log_connection_info(info, "blocked");
        free_http_request(&request);
        return -1;
    }
    
    // Check for suspicious request patterns
    if (is_suspicious_request(&request)) {
        printf("Suspicious request detected from %s\n", info->ip_address);
        info->flagged_suspicious = 1;
        
        // Only add to blacklist if it's a serious threat
        char *user_agent = get_header_value(&request, "User-Agent");
        if (is_suspicious_user_agent(user_agent)) {
            firewall_add_to_blacklist(info->ip_address, BLOCK_REASON_SUSPICIOUS, "Malicious user agent");
            log_connection_info(info, "suspicious");
            free_http_request(&request);
            return -1;
        }
    }
    
    // If request contains JSON, use optimized tokenizer
    if (request.content_type && strstr(request.content_type, "application/json")) {
        JSONValue json_result;
        if (parse_json_with_fast_tokenizer(request.body, request.body_length, &json_result) == 0) {
            // You can use the parsed JSON here
            free(json_result.key);
            free(json_result.value.str);
        }
    }
    
    // Route request
    RouteResponse response;
    
    if (route_request(server, &request, &response) != 0) {
        // Error routing
        const char *error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(client_fd, error_response, strlen(error_response), 0);
        free_http_request(&request);
        return -1;
    }
    
    // Keep-Alive Logic (FIXED)
    int keep_alive = 0;
    // Check if client requested Keep-Alive
    char *conn_header = get_header_value(&request, "Connection");
    if (conn_header && strstr(conn_header, "keep-alive")) {
        keep_alive = 1;
    }

    // If client wants keep-alive AND response is OK, patch the response header
    if (keep_alive && response.status_code == 200) {
        // Router typically sets "Connection: close". We need to patch it.
        char *conn_close_ptr = strstr(response.data, "Connection: close");
        if (conn_close_ptr) {
            // FIX: Realloc to expand buffer to prevent heap corruption
            // "Connection: close" (17 bytes) -> "Connection: keep-alive" (22 bytes)
            // We need 5 extra bytes.
            ptrdiff_t offset = conn_close_ptr - response.data; // Save offset before realloc
            
            char *new_data = realloc(response.data, response.length + 5);
            if (new_data) {
                response.data = new_data;
                conn_close_ptr = new_data + offset; // Update pointer after realloc
                
                // Move the rest of the string to the right by 5 bytes
                memmove(conn_close_ptr + 22, conn_close_ptr + 17, strlen(conn_close_ptr + 17) + 1);
                memcpy(conn_close_ptr, "Connection: keep-alive", 22);
                response.length += 5; // Update length
            } else {
                // Realloc failed, fallback to close
                keep_alive = 0;
            }
        }
        
        // Send response
        send(client_fd, response.data, response.length, 0);
        
        // Update Stats
        server->stats.total_requests++;
        server->stats.total_responses++;
        server->stats.bytes_sent += response.length;
        if (info) info->bytes_sent += response.length;

        // Free memory but DO NOT close socket (Return 0)
        free_http_request(&request);
        free_route_response(&response);
        
        return 0; // Keep connection alive!
    } 
    else {
        // Normal behavior: Close connection
        if (response.is_streaming) {
            stream_response(client_fd, &response);
        } else {
            send(client_fd, response.data, response.length, 0);
        }
        
        // Update statistics
        server->stats.total_requests++;
        server->stats.total_responses++;
        server->stats.bytes_sent += response.length;
        
        if (info) {
            info->bytes_sent += response.length;
        }
        
        // Free request and response memory
        free_http_request(&request);
        free_route_response(&response);
        
        return -1; // Signal to worker thread to close connection
    }
}

int server_send_response(Server *server, int client_fd, const char *response, size_t length) {
    ssize_t bytes_sent = send(client_fd, response, length, 0);
    if (bytes_sent < 0) {
        perror("send");
        return -1;
    }
    
    server->stats.bytes_sent += bytes_sent;
    
    // Update connection info
    ConnectionInfo *info = find_connection_info(client_fd);
    if (info) {
        info->bytes_sent += bytes_sent;
        info->last_activity = time(NULL); 
    }
    
    return 0;
}

// ===== Enhanced Server Functions =====
int server_get_socket(Server *server) {
    return server->server_fd;
}

int server_get_active_connections(Server *server) {
    return server->active_connections;
}

int server_get_connection_info(int client_fd, ConnectionInfo *info) {
    ConnectionInfo *conn_info = find_connection_info(client_fd);
    if (!conn_info) {
        return -1;
    }
    
    memcpy(info, conn_info, sizeof(ConnectionInfo));
    return 0;
}

int server_get_firewall_stats(FirewallStats *stats) {
    return firewall_get_stats(stats);
}

int server_add_to_whitelist(const char *ip_address, int permanent, time_t duration) {
    return firewall_add_to_whitelist(ip_address, permanent, duration);
}

int server_add_to_blacklist(const char *ip_address, BlockReason reason, const char *description) {
    return firewall_add_to_blacklist(ip_address, reason, description);
}

int server_block_ip(const char *ip_address) {
    return firewall_block_ip(ip_address);
}

int server_unblock_ip(const char *ip_address) {
    return firewall_unblock_ip(ip_address);
}

int server_get_blocked_ips(char ***blocked_ips, int *count) {
    return firewall_get_blocked_ips(blocked_ips, count);
}

int server_get_suspicious_ips(char ***suspicious_ips, int *count, int threshold) {
    return firewall_get_suspicious_ips(suspicious_ips, count, threshold);
}

int server_save_firewall_config(const char *filename) {
    return firewall_save_config(filename);
}

int server_load_firewall_config(const char *filename) {
    return firewall_load_config(filename);
}

int server_export_firewall_stats(const char *filename, int format) {
    return firewall_export_stats(filename, format);
}
