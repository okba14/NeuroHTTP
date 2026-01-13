#ifndef AIONIC_SERVER_H
#define AIONIC_SERVER_H

#include <stdint.h>
#include <signal.h>
#include "config.h"

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
     thread
    volatile sig_atomic_t running; 
    struct {
        uint64_t total_requests;
        uint64_t total_responses;
        uint64_t bytes_sent;
        uint64_t bytes_received;
        double avg_response_time;
    } stats;
} Server;

int server_init(Server *server, const Config *config);
int server_start(Server *server);
int server_stop(Server *server);
void server_cleanup(Server *server);
int server_process_events(Server *server);
int server_handle_request(Server *server, int client_fd);
int server_send_response(Server *server, int client_fd, const char *response, size_t length);

#endif // AIONIC_SERVER_H
