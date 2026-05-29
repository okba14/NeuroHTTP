#ifndef AIONIC_SERVER_H
#define AIONIC_SERVER_H

#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include "config.h"
#include "iouring_engine.h"
#include "ratelimiter.h"
#include "observability.h"
#include "arena.h"
#include "tls.h"
#include "http2.h"

#define CONN_HASH_SIZE 65536
#define MAX_SHUTDOWN_WAIT_MS 30000

typedef enum {
    CONN_STATE_NEW,
    CONN_STATE_TLS_HANDSHAKE,
    CONN_STATE_ACTIVE,
    CONN_STATE_H2_SESSION,
    CONN_STATE_DRAINING,
    CONN_STATE_CLOSED
} ConnState;

typedef struct ConnHashEntry {
    int client_fd;
    struct ConnectionInfo *info;
    struct ConnHashEntry *next;
} ConnHashEntry;

typedef struct WorkItem {
    int client_fd;
    struct WorkItem *next;
} WorkItem;

typedef struct ConnectionInfo ConnectionInfo;

struct ConnectionInfo {
    int client_fd;
    int epoll_owner_id;
    char ip_address[INET_ADDRSTRLEN];
    time_t connection_time;
    time_t last_activity;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    int requests_handled;
    int flagged_suspicious;
    char *partial_data;
    size_t partial_size;
    size_t partial_capacity;
    int content_length;
    int headers_done;
    char request_id[32];
    uint64_t request_start_ns;
    int keep_alive;
    ConnState state;
    TLSConn *tls_conn;
    H2Session *h2_session;
    int is_h2_upgrade;
    struct ConnectionInfo *hash_next;
};

typedef struct {
    int server_fd;
    int tls_server_fd;
    uint16_t port;
    uint16_t tls_port;
    int thread_count;
    void *thread_pool;
    void *connection_pool;
    void *request_queue;
    int max_connections;
    int active_connections;
    int *epoll_fds;
    pthread_t thread;
    pthread_t reaper_thread;
    volatile sig_atomic_t running;
    struct {
        uint64_t total_requests;
        uint64_t total_responses;
        uint64_t bytes_sent;
        uint64_t bytes_received;
        double avg_response_time;
    } stats;
    EventLoop *event_loop;
    EventLoop *tls_event_loop;
    RateLimiter *ratelimiter;
    ArenaPool *arenapool;
    int use_iouring;
    int use_zero_copy;
    int use_streaming;
    int keepalive_timeout;
    int shutdown_timeout;
    volatile sig_atomic_t shutdown_phase;

    WorkItem *work_queue_head;
    WorkItem *work_queue_tail;
    pthread_mutex_t work_queue_mutex;
    pthread_cond_t work_queue_cond;

    TLSCtx *tls_ctx;
    int enable_tls;
    int enable_http2;
    TLSConfig tls_config;
} Server;

int server_init(Server *server, const Config *config);
int server_start(Server *server);
int server_stop(Server *server);
void server_cleanup(Server *server);
int server_process_events(Server *server);
int server_handle_request(Server *server, int client_fd);
int server_send_response(Server *server, int client_fd, const char *response, size_t length);
void server_drain_connections(Server *server, int timeout_ms);
int server_connection_count(Server *server);

#endif
