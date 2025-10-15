#ifndef AIONIC_ROUTER_H
#define AIONIC_ROUTER_H

#include "parser.h"

typedef struct {
    char *path;
    HTTPMethod method;
    int (*handler)(HTTPRequest *request, RouteResponse *response);
} Route;

int route_request(HTTPRequest *request, RouteResponse *response);
void free_route_response(RouteResponse *response);
int register_route(const char *path, HTTPMethod method, int (*handler)(HTTPRequest *, RouteResponse *));
void init_routes(void);

int handle_chat_request(HTTPRequest *request, RouteResponse *response);
int handle_stats_request(HTTPRequest *request, RouteResponse *response);
int handle_health_request(HTTPRequest *request, RouteResponse *response);
int handle_root_request(HTTPRequest *request, RouteResponse *response);

#endif // AIONIC_ROUTER_H
