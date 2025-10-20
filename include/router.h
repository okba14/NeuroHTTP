#ifndef AIONIC_ROUTER_H
#define AIONIC_ROUTER_H

#include "parser.h"
#include <pthread.h>

// Maximum number of routes in the hash table
#define HASH_TABLE_SIZE 256

// Route structure with parameter handling
typedef struct Route {
    char *path;                      // Original path (e.g., "/users/:id")
    HTTPMethod method;
    int (*handler)(HTTPRequest *, RouteResponse *);
    struct Route *next;              // Next route in the bucket (for chaining)
    int param_count;                 // Number of parameters in path
    char param_names[8][32];        // Parameter names (e.g., "id")
} Route;

// Hash table structure
typedef struct {
    Route *buckets[HASH_TABLE_SIZE];  // Array of buckets
    pthread_rwlock_t lock;            // Read-write lock for the table
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
int route_request(HTTPRequest *request, RouteResponse *response);
void free_route_response(RouteResponse *response);
int register_route(const char *path, HTTPMethod method, int (*handler)(HTTPRequest *, RouteResponse *));
void init_routes(void);
void router_cleanup(void);

// Route handlers
int handle_chat_request(HTTPRequest *request, RouteResponse *response);
int handle_stats_request(HTTPRequest *request, RouteResponse *response);
int handle_health_request(HTTPRequest *request, RouteResponse *response);
int handle_root_request(HTTPRequest *request, RouteResponse *response);

// Optimization functions
int register_middleware(MiddlewareFunc middleware);
void router_init(void);
int create_error_response(RouteResponse *response, RouteError error, int status_code);

#endif // AIONIC_ROUTER_H
