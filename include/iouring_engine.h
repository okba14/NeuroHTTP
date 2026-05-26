#ifndef AIONIC_IOURING_ENGINE_H
#define AIONIC_IOURING_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <signal.h>

typedef struct EventLoop EventLoop;

typedef struct EventLoopConfig {
    int use_io_uring;
    int use_epoll;
    int max_events;
    int timeout_ms;
    int tcp_fastopen;
    int zero_copy;
    int busy_poll;
} EventLoopConfig;

typedef void (*accept_cb)(EventLoop *el, int fd, void *userdata);
typedef int (*read_cb)(EventLoop *el, int fd, const char *data, size_t len, void *userdata);
typedef void (*write_cb)(EventLoop *el, int fd, void *userdata);
typedef void (*close_cb)(EventLoop *el, int fd, void *userdata);
typedef void (*error_cb)(EventLoop *el, int fd, int error, void *userdata);

typedef enum {
    EL_WRITE_BUF,
    EL_SENDFILE_BUF,
    EL_SPLICE_BUF
} EventBufType;

EventLoopConfig event_loop_default_config(void);
EventLoop *event_loop_create(EventLoopConfig cfg);
void event_loop_set_callbacks(EventLoop *el, accept_cb on_accept, read_cb on_read, write_cb on_write, close_cb on_close, error_cb on_error, void *userdata);
int event_loop_add_fd(EventLoop *el, int fd, int events);
int event_loop_mod_fd(EventLoop *el, int fd, int events);
int event_loop_del_fd(EventLoop *el, int fd);
int event_loop_add_listener(EventLoop *el, int fd);
int event_loop_add_signal_fd(EventLoop *el, int fd);
int event_loop_run(EventLoop *el, volatile sig_atomic_t *running);
int event_loop_write(EventLoop *el, int fd, const char *data, size_t len);
int event_loop_sendfile(EventLoop *el, int fd, int file_fd, off_t offset, size_t count);
int event_loop_splice(EventLoop *el, int fd_in, int fd_out, size_t len);
void event_loop_stop(EventLoop *el);
void event_loop_destroy(EventLoop *el);

extern volatile sig_atomic_t iouring_global_stop;
int iouring_available(void);

#endif
