#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <poll.h>
#include "server.h"
#include "parser.h"
#include "router.h"
#include "stream.h"
#include "utils.h"
#include "asm_utils.h"
#include "firewall.h"
#include "config.h"
#include "plugin.h"
#include "ai/prompt_router.h"
#include "observability.h"
#include "ratelimiter.h"
#include "arena.h"
#include "http_parser.h"
#include "cache.h"
#include "tls.h"
#include "http2.h"

#define HEADER_BUFFER_SIZE 65536
#define BODY_READ_CHUNK 131072
#define MAX_EVENTS 1024
#define BACKLOG 128

static int b64_decode(const char *in, unsigned char *out, int *outlen) {
    static unsigned char tbl[256] = {0};
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 256; i++) tbl[i] = 64;
        for (int i = 'A'; i <= 'Z'; i++) tbl[i] = i - 'A';
        for (int i = 'a'; i <= 'z'; i++) tbl[i] = i - 'a' + 26;
        for (int i = '0'; i <= '9'; i++) tbl[i] = i - '0' + 52;
        tbl['+'] = 62; tbl['/'] = 63; tbl['-'] = 62; tbl['_'] = 63;
        init = 1;
    }
    int len = (int)strlen(in);
    while (len > 0 && in[len-1] == '=') len--;
    int o = 0, buf = 0, bits = 0;
    for (int i = 0; i < len; i++) {
        int c = (unsigned char)in[i];
        if (c > 255 || tbl[c] > 63) continue;
        buf = (buf << 6) | tbl[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)(buf >> bits);
        }
    }
    *outlen = o;
    return 0;
}

typedef struct {
    Server *server;
    int epoll_fd;
    int id;
    int max_request_size;
} ThreadData;

static ConnectionInfo *connections = NULL;
static int connection_count = 0;
static int connection_capacity = 0;
static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
static int global_max_request_size = 33554432;

static ConnHashEntry *global_conn_hash[CONN_HASH_SIZE] = {0};

static int ensure_capacity(char **buf, size_t *capacity, size_t needed) {
    if (needed <= *capacity) return 0;
    size_t new_cap = *capacity ? *capacity * 2 : 65536;
    while (new_cap < needed) new_cap *= 2;
    if (new_cap > (size_t)global_max_request_size + 65536) return -1;
    char *new_buf = realloc(*buf, new_cap);
    if (!new_buf) return -1;
    *buf = new_buf;
    *capacity = new_cap;
    return 0;
}

static void free_partial_data(ConnectionInfo *info) {
    free(info->partial_data);
    info->partial_data = NULL;
    info->partial_size = 0;
    info->partial_capacity = 0;
    info->content_length = 0;
    info->headers_done = 0;
}

static unsigned int conn_hash_fd(int fd) {
    return (unsigned int)(fd * 2654435761U) % CONN_HASH_SIZE;
}

static void conn_hash_add(int client_fd, ConnectionInfo *info) {
    unsigned int idx = conn_hash_fd(client_fd);
    ConnHashEntry *e = malloc(sizeof(ConnHashEntry));
    if (!e) return;
    e->client_fd = client_fd;
    e->info = info;
    e->next = global_conn_hash[idx];
    global_conn_hash[idx] = e;
}

static void conn_hash_remove(int client_fd) {
    unsigned int idx = conn_hash_fd(client_fd);
    ConnHashEntry **pp = &global_conn_hash[idx];
    while (*pp) {
        ConnHashEntry *e = *pp;
        if (e->client_fd == client_fd) { *pp = e->next; free(e); return; }
        pp = &e->next;
    }
}

static ConnectionInfo *add_connection_info(int client_fd, const char *ip_address, int thread_id) {
    pthread_mutex_lock(&connection_mutex);
    if (connection_count >= connection_capacity) {
        int new_cap = connection_capacity == 0 ? 128 : connection_capacity * 2;
        ConnectionInfo *new_conns = realloc(connections, sizeof(ConnectionInfo) * new_cap);
        if (!new_conns) { pthread_mutex_unlock(&connection_mutex); return NULL; }
        connections = new_conns;
        connection_capacity = new_cap;
    }
    time_t now = time(NULL);
    ConnectionInfo *info = &connections[connection_count];
    memset(info, 0, sizeof(ConnectionInfo));
    info->client_fd = client_fd;
    info->epoll_owner_id = thread_id;
    strncpy(info->ip_address, ip_address, INET_ADDRSTRLEN - 1);
    info->ip_address[INET_ADDRSTRLEN - 1] = '\0';
    info->connection_time = now;
    info->last_activity = now;
    info->state = CONN_STATE_NEW;
    obs_generate_request_id(info->request_id, sizeof(info->request_id));
    info->request_start_ns = get_current_time_ns();
    connection_count++;
    conn_hash_add(client_fd, info);
    pthread_mutex_unlock(&connection_mutex);
    return info;
}

static ConnectionInfo *find_connection_info(int client_fd) {
    unsigned int idx = conn_hash_fd(client_fd);
    ConnHashEntry *e = global_conn_hash[idx];
    while (e) {
        if (e->client_fd == client_fd) return e->info;
        e = e->next;
    }
    pthread_mutex_lock(&connection_mutex);
    for (int i = 0; i < connection_count; i++) {
        if (connections[i].client_fd == client_fd) { pthread_mutex_unlock(&connection_mutex); return &connections[i]; }
    }
    pthread_mutex_unlock(&connection_mutex);
    return NULL;
}

static void remove_connection_info(int client_fd) {
    conn_hash_remove(client_fd);
    pthread_mutex_lock(&connection_mutex);
    for (int i = 0; i < connection_count; i++) {
        if (connections[i].client_fd == client_fd) {
            ConnectionInfo *info = &connections[i];
            if (info->tls_conn) { tls_conn_destroy(info->tls_conn); info->tls_conn = NULL; }
            if (info->h2_session) { h2_session_destroy(info->h2_session); info->h2_session = NULL; }
            free_partial_data(info);
            if (i < connection_count - 1) connections[i] = connections[connection_count - 1];
            connection_count--;
            break;
        }
    }
    pthread_mutex_unlock(&connection_mutex);
}

static char *get_header_value(const HTTPRequest *request, const char *header_name) {
    size_t name_len = strlen(header_name);
    for (int i = 0; i < request->header_count; i++) {
        if (request->headers[i] && strncmp(request->headers[i], header_name, name_len) == 0 && request->headers[i][name_len] == ':') {
            char *value = request->headers[i] + name_len + 1;
            while (*value && isspace(*value)) value++;
            return value;
        }
    }
    return NULL;
}

static int is_suspicious_user_agent(const char *ua) {
    if (!ua) return 0;
    if (strstr(ua, "sqlmap") || strstr(ua, "nikto") || strstr(ua, "nmap") ||
        strstr(ua, "w3af") || strstr(ua, "burp") || strstr(ua, "metasploit")) return 1;
    return 0;
}

static int is_suspicious_request(const HTTPRequest *request) {
    if (request->path) {
        if (strstr(request->path, "../") || strstr(request->path, "..\\")) return 1;
        if ((strstr(request->path, ".php") || strstr(request->path, ".asp") || strstr(request->path, ".jsp") || strstr(request->path, ".exe")) &&
            !strstr(request->path, "/api/") && !strstr(request->path, "/static/")) return 1;
    }
    char *ua = get_header_value(request, "User-Agent");
    if (is_suspicious_user_agent(ua)) return 1;
    if (request->body_length > 10000000) return 1;
    if (request->content_type && request->body_length > 1000000) {
        if ((strstr(request->content_type, "application/x-www-form-urlencoded") || strstr(request->content_type, "multipart/form-data")) &&
            request->body && (strstr(request->body, " UNION ") || strstr(request->body, " OR ") || strstr(request->body, " AND "))) return 1;
    }
    return 0;
}

static int read_request_data_impl(Server *server, ConnectionInfo *info, int client_fd, char **out_data, size_t *out_size);

static int contains_attack_pattern(const char *data, const char *pattern) {
    if (!data || !pattern) return 0;
    char *found = strstr(data, pattern);
    if (!found) return 0;
    size_t pattern_len = strlen(pattern);
    size_t data_len = strlen(data);
    size_t found_pos = found - data;
    char before = found_pos > 0 ? data[found_pos - 1] : ' ';
    char after = found_pos + pattern_len < data_len ? data[found_pos + pattern_len] : ' ';
    if (strstr(pattern, "SELECT") || strstr(pattern, "INSERT") || strstr(pattern, "UPDATE") ||
        strstr(pattern, "DELETE") || strstr(pattern, "UNION") || strstr(pattern, "DROP")) {
        if ((!isalnum(before) && before != ' ') && (!isalnum(after) && after != ' ')) return 1;
    }
    if (strstr(pattern, "<script") || strstr(pattern, "javascript:")) {
        if (strstr(data, "<") || strstr(data, "onload=") || strstr(data, "onerror=")) return 1;
    }
    return 0;
}

static int handle_tls_accept(ConnectionInfo *info, Server *server) {
    if (!info->tls_conn) {
        info->tls_conn = tls_conn_new(server->tls_ctx, info->client_fd);
        if (!info->tls_conn) { info->state = CONN_STATE_CLOSED; return -1; }
        info->state = CONN_STATE_TLS_HANDSHAKE;
    }
    int ret = tls_conn_accept(info->tls_conn, 5000);
    if (ret == TLS_ERR_WANT_READ || ret == TLS_ERR_WANT_WRITE) { return 0; }
    if (ret != TLS_OK) { info->state = CONN_STATE_CLOSED; return -1; }
    info->state = CONN_STATE_ACTIVE;
    return 0;
}

static int tls_read_request_data(Server *server, ConnectionInfo *info, int client_fd, char **out_data, size_t *out_size) {
    (void)server;
    if (!info->tls_conn) return -1;
    char tmp[65536];
    int n = tls_conn_read(info->tls_conn, tmp, sizeof(tmp), 5000);
    if (n <= 0) {
        if (n == TLS_ERR_WANT_READ || n == TLS_ERR_WANT_WRITE) return 1;
        return -1;
    }
    info->bytes_received += (size_t)n;
    info->last_activity = time(NULL);
    if (ensure_capacity(&info->partial_data, &info->partial_capacity, info->partial_size + (size_t)n + 1) != 0) return -1;
    memcpy_dispatch(info->partial_data + info->partial_size, tmp, (size_t)n);
    info->partial_size += (size_t)n;
    info->partial_data[info->partial_size] = '\0';
    if (!info->headers_done) {
        char *header_end = strstr(info->partial_data, "\r\n\r\n");
        if (header_end) {
            info->headers_done = 1;
            char *cl = strstr(info->partial_data, "Content-Length:");
            if (!cl) cl = strstr(info->partial_data, "Content-length:");
            if (!cl) cl = strstr(info->partial_data, "content-length:");
            if (cl) {
                cl += 15;
                while (*cl && (*cl == ' ' || *cl == ':')) cl++;
                info->content_length = atoi(cl);
                if (info->content_length > global_max_request_size) return -2;
            }
            size_t header_end_pos = (size_t)((header_end + 4) - info->partial_data);
            size_t body_received = info->partial_size - header_end_pos;
            if (info->content_length > 0 && body_received < (size_t)info->content_length) return 1;
        }
    } else {
        char *header_end = strstr(info->partial_data, "\r\n\r\n");
        if (header_end) {
            size_t header_end_pos = (size_t)((header_end + 4) - info->partial_data);
            size_t body_received = info->partial_size - header_end_pos;
            if (info->content_length > 0 && body_received < (size_t)info->content_length) return 1;
        }
    }
    *out_data = info->partial_data;
    *out_size = info->partial_size;
    return 0;
}

static int tls_send_response(Server *server, ConnectionInfo *info, const char *data, size_t len) {
    (void)server;
    if (!info->tls_conn) return -1;
    int ret = tls_conn_write(info->tls_conn, data, len, 5000);
    if (ret < 0 && ret != TLS_ERR_WANT_READ && ret != TLS_ERR_WANT_WRITE) return -1;
    return 0;
}

static int check_h2_upgrade(ConnectionInfo *info) {
    if (info->is_h2_upgrade) return 1;
    if (!info->partial_data) return 0;
    char *upgrade = strstr(info->partial_data, "Upgrade:");
    if (!upgrade) {
        upgrade = strstr(info->partial_data, "upgrade:");
        if (!upgrade) {
            upgrade = strstr(info->partial_data, "UPGRADE:");
        }
    }
    if (upgrade && strstr(upgrade, "h2c")) return 1;
    return 0;
}

static int check_h2_prior_knowledge(ConnectionInfo *info) {
    return info->partial_data && strncmp(info->partial_data, "PRI * HTTP/2.0", 14) == 0;
}

static void *worker_thread_func(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    Server *server = data->server;
    int worker_id = data->id;
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { free(data); return NULL; }
    server->epoll_fds[worker_id] = epoll_fd;

    while (server->running) {
        struct epoll_event events[64];
        int nfds = epoll_wait(epoll_fd, events, 64, 100);
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                ConnectionInfo *info = find_connection_info(fd);
                if (info) info->state = CONN_STATE_CLOSED;
                close(fd);
                remove_connection_info(fd);
                server->active_connections--;
                continue;
            }
            if (events[i].events & EPOLLIN) {
                server_handle_request(server, fd);
            }
        }
        if (nfds < 0 && errno != EINTR) break;
    }
    close(epoll_fd);
    free(data);
    return NULL;
}

static void worker_add_fd(int epoll_fd, int client_fd) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = client_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
}

static void *reaper_thread(void *arg) {
    Server *server = (Server *)arg;
    while (server->running) {
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
        if (!server->running) break;
        int timeout = server->keepalive_timeout > 0 ? server->keepalive_timeout : 30;
        pthread_mutex_lock(&connection_mutex);
        time_t now = time(NULL);
        for (int i = connection_count - 1; i >= 0; i--) {
            ConnectionInfo *info = &connections[i];
            if (info->state == CONN_STATE_DRAINING) {
                if (info->last_activity > 0 && now - info->last_activity > 5) {
                    if (info->epoll_owner_id >= 0 && info->epoll_owner_id < server->thread_count) {
                        int target_epoll = server->epoll_fds[info->epoll_owner_id];
                        epoll_ctl(target_epoll, EPOLL_CTL_DEL, info->client_fd, NULL);
                    }
                    if (info->tls_conn) tls_conn_close(info->tls_conn);
                    else close(info->client_fd);
                    info->state = CONN_STATE_CLOSED;
                    server->active_connections--;
                    if (i < connection_count - 1) connections[i] = connections[connection_count - 1];
                    connection_count--;
                    i++;
                }
                continue;
            }
            if (info->state == CONN_STATE_ACTIVE && info->last_activity > 0 && now - info->last_activity > timeout) {
                if (info->epoll_owner_id >= 0 && info->epoll_owner_id < server->thread_count) {
                    int target_epoll = server->epoll_fds[info->epoll_owner_id];
                    epoll_ctl(target_epoll, EPOLL_CTL_DEL, info->client_fd, NULL);
                }
                if (info->tls_conn) tls_conn_close(info->tls_conn);
                else close(info->client_fd);
                info->state = CONN_STATE_CLOSED;
                server->active_connections--;
                if (i < connection_count - 1) connections[i] = connections[connection_count - 1];
                connection_count--;
                i++;
            }
        }
        pthread_mutex_unlock(&connection_mutex);
    }
    return NULL;
}

static void on_accept(EventLoop *el, int fd, void *userdata) {
    Server *server = (Server *)userdata;
    (void)el;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    getpeername(fd, (struct sockaddr*)&addr, &addrlen);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int thread_id = server->active_connections % server->thread_count;
    ConnectionInfo *info = add_connection_info(fd, client_ip, thread_id);
    if (info) {
        server->active_connections++;
        if (server->enable_tls && server->tls_ctx) {
            struct sockaddr_in local_addr;
            socklen_t local_len = sizeof(local_addr);
            if (getsockname(fd, (struct sockaddr*)&local_addr, &local_len) == 0 &&
                ntohs(local_addr.sin_port) == server->tls_port) {
                info->tls_conn = tls_conn_new(server->tls_ctx, fd);
                if (info->tls_conn) {
                    info->state = CONN_STATE_TLS_HANDSHAKE;
                }
            }
        }
    }
    if (thread_id >= 0 && thread_id < server->thread_count && server->epoll_fds[thread_id] >= 0) {
        worker_add_fd(server->epoll_fds[thread_id], fd);
    }
}

static int on_read(EventLoop *el, int fd, const char *data, size_t len, void *userdata) {
    Server *server = (Server *)userdata;
    (void)el; (void)data; (void)len;
    return server_handle_request(server, fd);
}

static void on_close(EventLoop *el, int fd, void *userdata) {
    Server *server = (Server *)userdata;
    (void)el;
    remove_connection_info(fd);
    server->active_connections--;
}

static void on_error(EventLoop *el, int fd, int error, void *userdata) {
    (void)el; (void)fd; (void)error; (void)userdata;
}

static int stream_ai_response_worker(Server *server, int client_fd, const char *prompt, const char *model_name) {
    if (!server->use_streaming) return -1;
    ConnectionInfo *info = find_connection_info(client_fd);
    if (!info) return -1;

    StreamData stream;
    StreamConfig sconfig = {
        .buffer_size = 8192,
        .chunked_encoding = true,
        .timeout_ms = 30000,
        .non_blocking = false,
        .priority = 0
    };
    if (stream_init_ex(&stream, client_fd, &sconfig) != 0) return -1;

    char header[1024];
    int header_len;
    if (info->tls_conn) {
        header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n");
        tls_conn_write(info->tls_conn, header, (size_t)header_len, 5000);
    } else {
        header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n");
        send(client_fd, header, (size_t)header_len, 0);
    }

    char buf[4096];
    obs_generate_request_id(buf, sizeof(buf));
    char sse_id[64];
    snprintf(sse_id, sizeof(sse_id), "id: %s\r\n", buf);
    if (info->tls_conn) tls_conn_write(info->tls_conn, sse_id, strlen(sse_id), 5000);
    else send(client_fd, sse_id, strlen(sse_id), 0);

    char sse_event[] = "event: message\r\n";
    if (info->tls_conn) tls_conn_write(info->tls_conn, sse_event, strlen(sse_event), 5000);
    else send(client_fd, sse_event, strlen(sse_event), 0);

    char *ai_buf = malloc(65536);
    if (!ai_buf) { stream_cleanup(&stream); return -1; }
    ai_buf[0] = '\0';

    char actual_model[128];
    memset(actual_model, 0, sizeof(actual_model));

    uint64_t start = get_current_time_ns();
    int result = prompt_router_route(prompt, model_name, ai_buf, 65536, actual_model, sizeof(actual_model));
    uint64_t latency_us = (get_current_time_ns() - start) / 1000;

    const char *used_model = actual_model[0] ? actual_model : (model_name ? model_name : "default");

    if (result == 0 && ai_buf[0]) {
        char sse_model[512];
        snprintf(sse_model, sizeof(sse_model), "data: {\"model\": \"%s\"}\r\n\r\n", used_model);
        if (info->tls_conn) tls_conn_write(info->tls_conn, sse_model, strlen(sse_model), 5000);
        else send(client_fd, sse_model, strlen(sse_model), 0);

        size_t total = strlen(ai_buf);
        size_t chunk_sz = 128;
        size_t pos = 0;
        while (pos < total) {
            size_t to_send = (total - pos) > chunk_sz ? chunk_sz : (total - pos);
            char sse_data[512];
            int sse_len = snprintf(sse_data, sizeof(sse_data), "data: %.*s\r\n\r\n", (int)to_send, ai_buf + pos);
            if (info->tls_conn) {
                if (tls_conn_write(info->tls_conn, sse_data, (size_t)sse_len, 5000) < 0) break;
            } else {
                if (send(client_fd, sse_data, (size_t)sse_len, 0) < 0) break;
            }
            pos += to_send;
            struct timespec ts = {0, 5000000};
            nanosleep(&ts, NULL);
        }
    } else {
        char sse_error[1024];
        snprintf(sse_error, sizeof(sse_error),
            "data: {\"error\": \"All models failed\", \"last_model\": \"%s\", \"latency_us\": %lu}\r\n\r\n",
            used_model, (unsigned long)latency_us);
        if (info->tls_conn) tls_conn_write(info->tls_conn, sse_error, strlen(sse_error), 5000);
        else send(client_fd, sse_error, strlen(sse_error), 0);
    }

    char sse_done[] = "data: [DONE]\r\n\r\n";
    if (info->tls_conn) tls_conn_write(info->tls_conn, sse_done, strlen(sse_done), 5000);
    else send(client_fd, sse_done, strlen(sse_done), 0);
    free(ai_buf);
    stream_cleanup(&stream);

    return 0;
}

static int http_parser_to_request(HTTPParser *parser, const char *raw, size_t raw_len, HTTPRequest *req) {
    (void)raw; (void)raw_len;
    memset(req, 0, sizeof(HTTPRequest));
    req->method = parser->method;
    if (parser->path_start && parser->path_len > 0) {
        req->path = strndup(parser->path_start, parser->path_len);
    }
    if (parser->query_start && parser->query_len > 0) {
        req->query_string = strndup(parser->query_start, parser->query_len);
    }
    for (int i = 0; i < parser->header_count && i < 32; i++) {
        req->headers[i] = strndup(parser->header_ptrs[i], parser->header_lens[i]);
    }
    req->header_count = parser->header_count > 32 ? 32 : parser->header_count;
    if (parser->body_start && parser->body_len > 0) {
        req->body = strndup(parser->body_start, parser->body_len);
        req->body_length = (int)parser->body_len;
    }
    if (parser->content_type_start && parser->content_type_len > 0) {
        req->content_type = strndup(parser->content_type_start, parser->content_type_len);
    }
    return 0;
}

int server_handle_request(Server *server, int client_fd) {
    ConnectionInfo *info = find_connection_info(client_fd);
    if (!info) return -1;

    if (!server->running && info->state != CONN_STATE_DRAINING) {
        info->state = CONN_STATE_DRAINING;
        return -1;
    }

    if (info->state == CONN_STATE_TLS_HANDSHAKE) {
        int ret = handle_tls_accept(info, server);
        if (ret != 0) return ret;
        if (info->state == CONN_STATE_TLS_HANDSHAKE) return 0;
    }

    if (info->state == CONN_STATE_NEW && info->tls_conn) {
        info->state = CONN_STATE_TLS_HANDSHAKE;
        int ret = handle_tls_accept(info, server);
        if (ret != 0) return ret;
        if (info->state == CONN_STATE_TLS_HANDSHAKE) return 0;
    }

    if (info->state == CONN_STATE_H2_SESSION) {
        if (info->h2_session) {
            int ret;
            if (info->tls_conn) {
                char h2buf[65536];
                int n = tls_conn_read(info->tls_conn, h2buf, sizeof(h2buf), 5000);
                if (n == TLS_ERR_WANT_READ || n == TLS_ERR_WANT_WRITE) return 0;
                if (n < 0) { info->state = CONN_STATE_CLOSED; return -1; }
                ret = h2_session_feed_data(info->h2_session, h2buf, (size_t)n);
            } else {
                ret = h2_session_recv(info->h2_session);
            }
            if (ret == H2_ERR_CLOSED || ret == H2_ERR_PROTOCOL) {
                info->state = CONN_STATE_CLOSED;
                return -1;
            }
        }
        return 0;
    }

    if (info->state == CONN_STATE_NEW) {
        info->state = CONN_STATE_ACTIVE;
    }

    info->requests_handled++;
    char *request_data = NULL;
    size_t request_size = 0;
    int ret;

    if (info->tls_conn) {
        ret = tls_read_request_data(server, info, client_fd, &request_data, &request_size);
    } else {
        ret = read_request_data_impl(server, info, client_fd, &request_data, &request_size);
    }

    if (ret == -2) {
        const char *err = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (info->tls_conn) tls_conn_write(info->tls_conn, err, strlen(err), 5000);
        else send(client_fd, err, strlen(err), 0);
        free_partial_data(info);
        return -1;
    }
    if (ret == 1) { info->last_activity = time(NULL); return 0; }
    if (ret != 0 || !request_data) {
        const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (info->tls_conn) tls_conn_write(info->tls_conn, err, strlen(err), 5000);
        else send(client_fd, err, strlen(err), 0);
        free_partial_data(info);
        return -1;
    }

    if (!info->is_h2_upgrade && server->enable_http2 && check_h2_upgrade(info)) {
        info->is_h2_upgrade = 1;
    }

    if (info->is_h2_upgrade && server->enable_http2) {
        H2Config h2cfg;
        memset(&h2cfg, 0, sizeof(h2cfg));
        h2cfg.max_concurrent_streams = AIONIC_HTTP2_MAX_CONCURRENT_STREAMS;
        h2cfg.max_header_list_size = AIONIC_HTTP2_MAX_HEADER_LIST_SIZE;
        h2cfg.initial_window_size = AIONIC_HTTP2_INITIAL_WINDOW_SIZE;
        h2cfg.max_frame_size = AIONIC_HTTP2_DEFAULT_MAX_FRAME_SIZE;
        h2cfg.enable_push = 0;
        h2cfg.user_data = server;
        info->h2_session = h2_session_new(client_fd, &h2cfg, 1);
        if (info->h2_session) {
            info->state = CONN_STATE_H2_SESSION;
            const char *h2_upgrade_resp =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: h2c\r\n\r\n";
            send(client_fd, h2_upgrade_resp, strlen(h2_upgrade_resp), 0);
            char *h2s = strstr(info->partial_data, "HTTP2-Settings:");
            if (!h2s) h2s = strstr(info->partial_data, "Http2-Settings:");
            if (!h2s) h2s = strstr(info->partial_data, "http2-settings:");
            if (h2s) {
                h2s += 15;
                while (*h2s == ' ') h2s++;
                char *eol = strchr(h2s, '\r');
                if (!eol) eol = strchr(h2s, '\n');
                if (eol) {
                    size_t b64len = (size_t)(eol - h2s);
                    char *b64val = strndup(h2s, b64len);
                    unsigned char settings[128];
                    int settings_len = (int)sizeof(settings);
                    if (b64_decode(b64val, settings, &settings_len) == 0 && settings_len > 0) {
                        h2_session_upgrade(info->h2_session, (const char *)settings, (size_t)settings_len);
                    }
                    free(b64val);
                }
            }
            return 0;
        }
    }

    if (!info->is_h2_upgrade && server->enable_http2 && check_h2_prior_knowledge(info)) {
        H2Config h2cfg;
        memset(&h2cfg, 0, sizeof(h2cfg));
        h2cfg.max_concurrent_streams = AIONIC_HTTP2_MAX_CONCURRENT_STREAMS;
        h2cfg.max_header_list_size = AIONIC_HTTP2_MAX_HEADER_LIST_SIZE;
        h2cfg.initial_window_size = AIONIC_HTTP2_INITIAL_WINDOW_SIZE;
        h2cfg.max_frame_size = AIONIC_HTTP2_DEFAULT_MAX_FRAME_SIZE;
        h2cfg.enable_push = 0;
        h2cfg.user_data = server;
        info->h2_session = h2_session_new(client_fd, &h2cfg, 1);
        if (info->h2_session) {
            info->state = CONN_STATE_H2_SESSION;
            info->is_h2_upgrade = 1;
            h2_session_submit_settings(info->h2_session);
            h2_session_send(info->h2_session);
            h2_session_feed_data(info->h2_session, info->partial_data, info->partial_size);
            free_partial_data(info);
            return 0;
        }
    }

    HTTPParser parser;
    http_parser_init(&parser);
    HTTPParseError perr = http_parser_execute(&parser, request_data, request_size);
    if (perr != HP_OK && perr != HP_ERROR_INCOMPLETE) {
        perr = http_parser_finish(&parser);
    }
    if (perr != HP_OK) {
        if (contains_attack_pattern(request_data, "<script") || contains_attack_pattern(request_data, "javascript:") || contains_attack_pattern(request_data, "eval(")) {
            info->flagged_suspicious = 1;
            free_partial_data(info);
            return -1;
        }
        const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (info->tls_conn) tls_conn_write(info->tls_conn, err, strlen(err), 5000);
        else send(client_fd, err, strlen(err), 0);
        free_partial_data(info);
        return -1;
    }

    HTTPRequest request;
    if (http_parser_to_request(&parser, request_data, request_size, &request) != 0) {
        const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (info->tls_conn) tls_conn_write(info->tls_conn, err, strlen(err), 5000);
        else send(client_fd, err, strlen(err), 0);
        free_partial_data(info);
        return -1;
    }

    uint64_t request_start = get_current_time_ns();

    PluginContext pctx = {0};
    pctx.request = &request;
    pctx.client_fd = client_fd;
    pctx.client_ip = info->ip_address;
    plugin_process_hooks(PLUGIN_HOOK_PRE_REQUEST, &pctx);

    if (firewall_is_blacklisted(info->ip_address)) {
        info->flagged_suspicious = 1;
        free_http_request(&request);
        free_partial_data(info);
        return -1;
    }

    if (is_suspicious_request(&request)) {
        info->flagged_suspicious = 1;
        char *ua = get_header_value(&request, "User-Agent");
        if (is_suspicious_user_agent(ua)) {
            firewall_add_to_blacklist(info->ip_address, BLOCK_REASON_SUSPICIOUS, "Malicious user agent");
            free_http_request(&request);
            free_partial_data(info);
            return -1;
        }
    }

    if (server->ratelimiter) {
        if (!ratelimiter_allow(server->ratelimiter, info->ip_address)) {
            const char *err = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            if (info->tls_conn) tls_conn_write(info->tls_conn, err, strlen(err), 5000);
            else send(client_fd, err, strlen(err), 0);
            free_http_request(&request);
            free_partial_data(info);
            return -1;
        }
    }

    if (request.content_type && strstr(request.content_type, "application/json")) {
        JSONValue json_result;
        if (parse_json_with_fast_tokenizer(request.body, request.body_length, &json_result) == 0) {
            free(json_result.key);
            free(json_result.value.str);
        }
    }

    char *conn_header = get_header_value(&request, "Connection");
    int keep_alive = (conn_header && strstr(conn_header, "keep-alive")) ? 1 : 0;

    if (server->use_streaming && (int)request.method == (int)HTTP_POST && request.path &&
        strcmp(request.path, "/v1/chat/stream") == 0) {
        char *prompt = NULL;
        char *model_name = NULL;
        char prompt_buf[16384];
        if (parse_json(request.body, prompt_buf, sizeof(prompt_buf)) == 0) {
            prompt = prompt_buf;
        }
        if (!prompt && request.body) {
            size_t cl = request.body_length < (int)sizeof(prompt_buf) - 1 ? (size_t)request.body_length : sizeof(prompt_buf) - 1;
            memcpy(prompt_buf, request.body, cl);
            prompt_buf[cl] = 0;
            prompt = prompt_buf;
        }
        if (prompt) {
            int sr = stream_ai_response_worker(server, client_fd, prompt, model_name);
            free_http_request(&request);
            free_partial_data(info);
            uint64_t latency_us = (get_current_time_ns() - request_start) / 1000;
            obs_record_request("POST", "/v1/chat/stream", 200, latency_us, 0, sr != 0);
            return sr == 0 ? 0 : -1;
        }
    }

    RouteResponse response;
    Arena *req_arena = server->arenapool ? arenapool_get(server->arenapool) : NULL;
    if (req_arena) arena_reset(req_arena);

    if (route_request(server, &request, &response) != 0) {
        const char *err = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (info->tls_conn) tls_conn_write(info->tls_conn, err, strlen(err), 5000);
        else send(client_fd, err, strlen(err), 0);
        free_http_request(&request);
        free_partial_data(info);
        return -1;
    }

    pctx.response = &response;
    plugin_process_hooks(PLUGIN_HOOK_POST_REQUEST, &pctx);

    if (info->tls_conn) {
        if (keep_alive && response.status_code == 200) {
            tls_conn_write(info->tls_conn, response.data, response.length, 5000);
        } else {
            if (response.is_streaming) {
                stream_response(client_fd, &response);
            } else {
                tls_conn_write(info->tls_conn, response.data, response.length, 5000);
            }
        }
    } else {
        if (keep_alive && response.status_code == 200) {
            send(client_fd, response.data, response.length, 0);
        } else {
            if (response.is_streaming) stream_response(client_fd, &response);
            else send(client_fd, response.data, response.length, 0);
        }
    }

    server->stats.total_requests++;
    server->stats.total_responses++;
    server->stats.bytes_sent += response.length;
    if (info) info->bytes_sent += response.length;

    uint64_t latency_us = (get_current_time_ns() - request_start) / 1000;
    obs_record_request((int)request.method == (int)HTTP_GET ? "GET" : "POST", request.path ? request.path : "/", response.status_code, latency_us, 0, 0);

    free_http_request(&request);
    free_route_response(&response);
    free_partial_data(info);
    return keep_alive ? 0 : -1;
}

static int read_request_data_impl(Server *server, ConnectionInfo *info, int client_fd, char **out_data, size_t *out_size) {
    char tmp[65536];
    ssize_t n = recv(client_fd, tmp, sizeof(tmp), 0);
    if (n <= 0) return -1;
    server->stats.bytes_received += (size_t)n;
    info->bytes_received += (size_t)n;
    info->last_activity = time(NULL);
    if (ensure_capacity(&info->partial_data, &info->partial_capacity, info->partial_size + (size_t)n + 1) != 0) return -1;
    memcpy_dispatch(info->partial_data + info->partial_size, tmp, (size_t)n);
    info->partial_size += (size_t)n;
    info->partial_data[info->partial_size] = '\0';
    if (!info->headers_done) {
        char *header_end = strstr(info->partial_data, "\r\n\r\n");
        if (header_end) {
            info->headers_done = 1;
            char *cl = strstr(info->partial_data, "Content-Length:");
            if (!cl) cl = strstr(info->partial_data, "Content-length:");
            if (!cl) cl = strstr(info->partial_data, "content-length:");
            if (cl) {
                cl += 15;
                while (*cl && (*cl == ' ' || *cl == ':')) cl++;
                info->content_length = atoi(cl);
                if (info->content_length > global_max_request_size) return -2;
            }
            size_t header_end_pos = (header_end + 4) - info->partial_data;
            size_t body_received = info->partial_size - header_end_pos;
            if (info->content_length > 0 && body_received < (size_t)info->content_length) return 1;
        }
    } else {
        char *header_end = strstr(info->partial_data, "\r\n\r\n");
        if (header_end) {
            size_t header_end_pos = (header_end + 4) - info->partial_data;
            size_t body_received = info->partial_size - header_end_pos;
            if (info->content_length > 0 && body_received < (size_t)info->content_length) return 1;
        }
    }
    *out_data = info->partial_data;
    *out_size = info->partial_size;
    return 0;
}

int server_send_response(Server *server, int client_fd, const char *response, size_t length) {
    ConnectionInfo *info = find_connection_info(client_fd);
    if (!info) return -1;
    ssize_t bytes_sent;
    if (info->tls_conn) {
        int ret = tls_conn_write(info->tls_conn, response, length, 5000);
        if (ret < 0) return -1;
        bytes_sent = (ssize_t)length;
    } else {
        bytes_sent = send(client_fd, response, length, MSG_NOSIGNAL);
        if (bytes_sent < 0) return -1;
    }
    server->stats.bytes_sent += (uint64_t)bytes_sent;
    if (info) { info->bytes_sent += (uint64_t)bytes_sent; info->last_activity = time(NULL); }
    return 0;
}

int server_get_socket(Server *server) { return server->server_fd; }
int server_get_active_connections(Server *server) { return server->active_connections; }
int server_connection_count(Server *server) { (void)server; return connection_count; }

void server_drain_connections(Server *server, int timeout_ms) {
    if (!server) return;
    int waited = 0;
    int check_interval = 100;
    while (waited < timeout_ms) {
        pthread_mutex_lock(&connection_mutex);
        int active = 0;
        for (int i = 0; i < connection_count; i++) {
            if (connections[i].state == CONN_STATE_ACTIVE ||
                connections[i].state == CONN_STATE_TLS_HANDSHAKE ||
                connections[i].state == CONN_STATE_H2_SESSION) {
                connections[i].state = CONN_STATE_DRAINING;
                active++;
            }
        }
        pthread_mutex_unlock(&connection_mutex);
        if (active == 0) break;
        struct timespec ts = {0, (long)(check_interval * 1000000)};
        nanosleep(&ts, NULL);
        waited += check_interval;
    }
}

int server_init(Server *server, const Config *config) {
    memset(server, 0, sizeof(Server));
    server->port = (uint16_t)config->port;
    server->tls_port = (uint16_t)config->tls_port;
    server->thread_count = config->worker_threads > 0 ? config->worker_threads : config->thread_count;
    server->max_connections = config->max_connections;
    server->running = 0;
    server->use_iouring = config->enable_iouring;
    server->use_zero_copy = config->enable_zero_copy;
    server->use_streaming = config->enable_streaming;
    server->keepalive_timeout = config->keepalive_timeout > 0 ? config->keepalive_timeout : 30;
    server->shutdown_timeout = config->graceful_shutdown_timeout > 0 ? config->graceful_shutdown_timeout : 30;
    server->enable_tls = config->enable_tls;
    server->enable_http2 = config->enable_http2;
    server->stats.total_requests = 0;
    server->stats.total_responses = 0;
    server->stats.bytes_sent = 0;
    server->stats.bytes_received = 0;

    if (config->enable_observability) obs_init();
    if (config->enable_ratelimiter) {
        TokenBucketConfig rl_cfg;
        rl_cfg.tokens_per_sec = config->rate_limit_rps > 0 ? config->rate_limit_rps : 100;
        rl_cfg.bucket_size = rl_cfg.tokens_per_sec;
        rl_cfg.window_ms = 1000;
        rl_cfg.max_requests_per_window = rl_cfg.tokens_per_sec;
        rl_cfg.max_concurrent = config->max_connections;
        server->ratelimiter = ratelimiter_create(rl_cfg);
    }

    server->arenapool = arenapool_create(1048576, server->thread_count > 0 ? server->thread_count : 4);

    if (server->enable_tls) {
        TLSConfig tls_cfg;
        memset(&tls_cfg, 0, sizeof(tls_cfg));
        strncpy(tls_cfg.cert_file, config->tls_cert_file, sizeof(tls_cfg.cert_file) - 1);
        strncpy(tls_cfg.key_file, config->tls_key_file, sizeof(tls_cfg.key_file) - 1);
        strncpy(tls_cfg.ca_file, config->tls_ca_file, sizeof(tls_cfg.ca_file) - 1);
        strncpy(tls_cfg.ca_path, config->tls_ca_path, sizeof(tls_cfg.ca_path) - 1);
        strncpy(tls_cfg.cipher_list, config->tls_cipher_list[0] ? config->tls_cipher_list :
            "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256", sizeof(tls_cfg.cipher_list) - 1);
        strncpy(tls_cfg.cipher_suites_tls13, config->tls_ciphersuites_tls13, sizeof(tls_cfg.cipher_suites_tls13) - 1);
        tls_cfg.min_method = config->tls_min_version == 2 ? TLS_METHOD_TLSv1_2 : TLS_METHOD_TLSv1_3;
        tls_cfg.prefer_server_ciphers = config->tls_prefer_server_ciphers;
        tls_cfg.verify_client = config->tls_verify_client;
        tls_cfg.verify_depth = config->tls_verify_depth > 0 ? config->tls_verify_depth : 10;
        tls_cfg.enable_ocsp_stapling = config->tls_enable_ocsp;
        tls_cfg.ocsp_refresh_interval = config->tls_ocsp_refresh_interval > 0 ? config->tls_ocsp_refresh_interval : 3600;
        tls_cfg.session_cache_size = config->tls_session_cache_size > 0 ? config->tls_session_cache_size : 2048;
        tls_cfg.enable_renegotiation = config->tls_enable_renegotiation;
        tls_cfg.enable_early_data = config->tls_enable_early_data;
        tls_cfg.hsts_max_age = config->tls_hsts_max_age;
        tls_cfg.hsts_include_subdomains = config->tls_hsts_include_subdomains;
        strncpy(tls_cfg.alpn_protocols, config->tls_alpn_protocols[0] ? config->tls_alpn_protocols : "h2,http/1.1",
                sizeof(tls_cfg.alpn_protocols) - 1);

        server->tls_ctx = tls_ctx_create(&tls_cfg);
        if (!server->tls_ctx) {
            fprintf(stderr, "Failed to initialize TLS context\n");
            return -1;
        }
        memcpy(&server->tls_config, &tls_cfg, sizeof(TLSConfig));

        if (tls_cfg.enable_ocsp_stapling) {
            tls_ctx_ocsp_update(server->tls_ctx);
        }
    }

    EventLoopConfig el_cfg = event_loop_default_config();
    el_cfg.use_io_uring = config->enable_iouring;
    el_cfg.max_events = config->max_connections > 0 ? config->max_connections : 4096;
    el_cfg.io_uring_queue_depth = config->iouring_queue_depth > 0 ? config->iouring_queue_depth : 512;
    el_cfg.io_uring_sqpoll = config->iouring_sqpoll;
    server->event_loop = event_loop_create(el_cfg);
    if (!server->event_loop) { fprintf(stderr, "Failed to create event loop\n"); return -1; }

    event_loop_set_callbacks(server->event_loop, on_accept, on_read, NULL, on_close, on_error, server);

    server->server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->server_fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef TCP_DEFER_ACCEPT
    setsockopt(server->server_fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(opt));
#endif
    if (config->enable_keepalive) {
        int ka = 1;
        setsockopt(server->server_fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);
    if (bind(server->server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { perror("bind"); close(server->server_fd); return -1; }
    if (listen(server->server_fd, BACKLOG) < 0) { perror("listen"); close(server->server_fd); return -1; }

    event_loop_add_listener(server->event_loop, server->server_fd);

    if (server->enable_tls) {
        server->tls_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (server->tls_server_fd < 0) { perror("tls socket"); return -1; }
        setsockopt(server->tls_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef TCP_DEFER_ACCEPT
        setsockopt(server->tls_server_fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(opt));
#endif
        struct sockaddr_in tls_addr;
        memset(&tls_addr, 0, sizeof(tls_addr));
        tls_addr.sin_family = AF_INET;
        tls_addr.sin_addr.s_addr = INADDR_ANY;
        tls_addr.sin_port = htons(server->tls_port);
        if (bind(server->tls_server_fd, (struct sockaddr *)&tls_addr, sizeof(tls_addr)) < 0) { perror("tls bind"); close(server->tls_server_fd); return -1; }
        if (listen(server->tls_server_fd, BACKLOG) < 0) { perror("tls listen"); close(server->tls_server_fd); return -1; }
        event_loop_add_listener(server->event_loop, server->tls_server_fd);
        printf("[TLS] HTTPS listening on port %d\n", server->tls_port);
    }

    server->thread_pool = malloc(sizeof(pthread_t) * (size_t)server->thread_count);
    server->request_queue = malloc(sizeof(void *) * (size_t)server->max_connections);
    server->epoll_fds = malloc(sizeof(int) * (size_t)server->thread_count);

    init_routes();
    if (config->enable_firewall) {
        if (firewall_init(config) != 0) fprintf(stderr, "Failed to init firewall\n");
        firewall_add_attack_pattern("<script", 9);
        firewall_add_attack_pattern("javascript:", 8);
        firewall_add_attack_pattern("onload=", 8);
        firewall_add_attack_pattern("onerror=", 8);
        firewall_add_attack_pattern("alert(", 8);
        firewall_add_attack_pattern("document.cookie", 8);
        firewall_add_attack_pattern("eval(", 9);
        firewall_add_attack_pattern("iframe", 7);
    }

    return 0;
}

int server_start(Server *server) {
    server->running = 1;
    server->shutdown_phase = 0;
    pthread_mutex_init(&server->work_queue_mutex, NULL);
    pthread_cond_init(&server->work_queue_cond, NULL);

    for (int i = 0; i < server->thread_count; i++) {
        ThreadData *data = malloc(sizeof(ThreadData));
        if (!data) continue;
        data->server = server;
        data->id = i;
        data->max_request_size = global_max_request_size;
        data->epoll_fd = -1;
        server->epoll_fds[i] = -1;
        if (pthread_create(&((pthread_t *)server->thread_pool)[i], NULL, worker_thread_func, data) != 0) {
            free(data); continue;
        }
    }
    if (pthread_create(&server->reaper_thread, NULL, reaper_thread, server) != 0) perror("pthread_create reaper");
    return 0;
}

int server_stop(Server *server) {
    printf("[SHUTDOWN] Initiating graceful shutdown (timeout: %ds)...\n", server->shutdown_timeout);
    server->running = 0;
    server->shutdown_phase = 1;

    event_loop_stop(server->event_loop);

    server_drain_connections(server, server->shutdown_timeout * 1000);

    pthread_join(server->reaper_thread, NULL);

    for (int i = 0; i < server->thread_count; i++) {
        if (server->epoll_fds[i] >= 0) close(server->epoll_fds[i]);
        if (((pthread_t *)server->thread_pool)[i]) pthread_join(((pthread_t *)server->thread_pool)[i], NULL);
    }

    pthread_mutex_lock(&connection_mutex);
    for (int i = 0; i < connection_count; i++) {
        ConnectionInfo *info = &connections[i];
        if (info->tls_conn) tls_conn_close(info->tls_conn);
        else if (info->client_fd >= 0) close(info->client_fd);
        if (info->h2_session) h2_session_destroy(info->h2_session);
        free_partial_data(info);
    }
    free(connections); connections = NULL;
    connection_count = 0; connection_capacity = 0;
    pthread_mutex_unlock(&connection_mutex);

    if (server->server_fd >= 0) close(server->server_fd);
    if (server->tls_server_fd >= 0) close(server->tls_server_fd);

    printf("[SHUTDOWN] Server stopped gracefully\n");
    return 0;
}

void server_cleanup(Server *server) {
    free(server->thread_pool);
    free(server->request_queue);
    free(server->epoll_fds);
    if (server->event_loop) event_loop_destroy(server->event_loop);
    if (server->ratelimiter) ratelimiter_destroy(server->ratelimiter);
    if (server->arenapool) arenapool_destroy(server->arenapool);
    if (server->tls_ctx) tls_ctx_destroy(server->tls_ctx);
    obs_cleanup();
}

int server_process_events(Server *server) {
    return event_loop_run(server->event_loop, &server->running);
}
