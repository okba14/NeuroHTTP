#ifndef AIONIC_SERVER_H
#define AIONIC_SERVER_H

#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include "config.h"
#include "iouring_engine.h"
#include "ratelimiter.h"
#include "observability.h"
#include "arena.h"

#define CONN_HASH_SIZE 65536

typedef struct ConnHashEntry {
    int client_fd;
    struct ConnectionInfo *info;
    struct ConnHashEntry *next;
} ConnHashEntry;

typedef struct WorkItem {
    int client_fd;
    struct WorkItem *next;
} WorkItem;

typedef struct {
    int server_fd;
    uint16_t port;
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
    RateLimiter *ratelimiter;
    ArenaPool *arenapool;
    int use_iouring;
    int use_zero_copy;
    int use_streaming;
    int keepalive_timeout;

    WorkItem *work_queue_head;
    WorkItem *work_queue_tail;
    pthread_mutex_t work_queue_mutex;
    pthread_cond_t work_queue_cond;
} Server;

int server_init(Server *server, const Config *config);
int server_start(Server *server);
int server_stop(Server *server);
void server_cleanup(Server *server);
int server_process_events(Server *server);
int server_handle_request(Server *server, int client_fd);
int server_send_response(Server *server, int client_fd, const char *response, size_t length);

#endif
