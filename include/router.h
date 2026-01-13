#ifndef AIONIC_ROUTER_H
#define AIONIC_ROUTER_H

#include "parser.h"
#include "server.h" 
#include <pthread.h>

// Maximum number of routes in the hash table
#define HASH_TABLE_SIZE 256

// Route structure with parameter handling
typedef struct Route {
    char *path;                      
    HTTPMethod method;
    // Updated handler signature to include Server * for stats access
    int (*handler)(Server *, HTTPRequest *, RouteResponse *); 
    struct Route *next;              
    int param_count;                 
    char param_names[8][32];        
} Route;

// Hash table structure
typedef struct {
    Route *buckets[HASH_TABLE_SIZE];  
    pthread_rwlock_t lock;            
} RouteHashTable;

// Error codes for better error handling
typedef enum {
    ROUTE_ERROR_NONE = 0,
    ROUTE_ERROR_MEMORY,
    ROUTE_ERROR_INVALID_PARAM,
    ROUTE_ERROR_NOT_FOUND,
    ROUTE_ERROR_INTERNAL
} RouteError;

// Middleware function type
typedef int (*MiddlewareFunc)(HTTPRequest *, RouteResponse *);

// Core routing functions
int route_request(Server *server, HTTPRequest *request, RouteResponse *response); // Updated signature
void free_route_response(RouteResponse *response);
int register_route(const char *path, HTTPMethod method, int (*handler)(Server *, HTTPRequest *, RouteResponse *)); // Updated signature
void init_routes(void);
void router_cleanup(void);

// Route handlers
int handle_chat_request(Server *server, HTTPRequest *request, RouteResponse *response); // Updated signature
int handle_stats_request(Server *server, HTTPRequest *request, RouteResponse *response); // Updated signature
int handle_health_request(Server *server, HTTPRequest *request, RouteResponse *response); // Updated signature
int handle_root_request(Server *server, HTTPRequest *request, RouteResponse *response); // Updated signature

// Optimization functions
int register_middleware(MiddlewareFunc middleware);
void router_init(void);
int create_error_response(RouteResponse *response, RouteError error, int status_code);

#endif // AIONIC_ROUTER_H
