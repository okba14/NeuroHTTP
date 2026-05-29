#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <linux/io_uring.h>
#include "iouring_engine.h"

#define MAX_EVENTS_DEFAULT 4096
#define IOURING_QUEUE_DEPTH 512
#define IOURING_MAX_CQES 512

volatile sig_atomic_t iouring_global_stop = 0;

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

struct io_ring {
    int ring_fd;
    struct io_uring_sqe *sqes;
    struct io_uring_cqe *cqes;
    unsigned int *shead;
    unsigned int *stail;
    unsigned int *sring_mask;
    unsigned int *sring_entries;
    unsigned int *cring_mask;
    unsigned int *cring_entries;
    unsigned int *chead;
    unsigned int *ctail;
    unsigned int sqes_mask;
    unsigned int sqes_entries;
    unsigned int sq_head;
    unsigned int sq_tail;
    unsigned int cached_cqe;
    unsigned int pending_count;
    char *sq_ring_base;
    char *sq_sqes_base;
    char *cq_ring_base;
    size_t sq_sqes_size;
    size_t sq_ring_size;
    size_t cq_ring_size;
    int features;
    int use_sqpoll;
};

typedef struct WriteBuf {
    char *data;
    size_t len;
    size_t sent;
    int fd;
    int is_sendfile;
    int file_fd;
    off_t offset;
    struct WriteBuf *next;
} WriteBuf;

typedef struct IOUringData {
    struct io_ring ring;
    int use_io_uring;
    int sqpoll_enabled;
    struct epoll_event *epoll_events;
    int epoll_fd;
} IOUringData;

struct EventLoop {
    int epoll_fd;
    int event_fd;
    int use_io_uring;
    int use_epoll;
    int max_events;
    int timeout_ms;
    int tcp_fastopen;
    int zero_copy;
    int busy_poll;
    accept_cb on_accept;
    read_cb on_read;
    write_cb on_write;
    close_cb on_close;
    error_cb on_error;
    void *userdata;
    int listener_fds[64];
    int listener_count;
    WriteBuf write_queue;
    int write_queue_count;
    pthread_mutex_t write_mutex;
    IOUringData iou;
    int conn_count;
    int conn_capacity;
    int *conn_fds;
};

EventLoopConfig event_loop_default_config(void) {
    EventLoopConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.use_io_uring = 1;
    cfg.use_epoll = 1;
    cfg.max_events = MAX_EVENTS_DEFAULT;
    cfg.timeout_ms = 100;
    cfg.tcp_fastopen = 0;
    cfg.zero_copy = 0;
    cfg.busy_poll = 0;
    cfg.io_uring_queue_depth = IOURING_QUEUE_DEPTH;
    cfg.io_uring_sqpoll = 0;
    return cfg;
}

int iouring_available(void) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    int fd = (int)syscall(__NR_io_uring_setup, 1, &params);
    if (fd >= 0) { close(fd); return 1; }
    return 0;
}

static int iouring_ring_setup(struct io_ring *ring, int entries, int use_sqpoll) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    if (use_sqpoll) params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000;
    ring->ring_fd = (int)syscall(__NR_io_uring_setup, (long)entries, &params);
    if (ring->ring_fd < 0) {
        if (use_sqpoll) {
            params.flags &= ~IORING_SETUP_SQPOLL;
            ring->ring_fd = (int)syscall(__NR_io_uring_setup, (long)entries, &params);
            if (ring->ring_fd < 0) return -1;
        } else {
            return -1;
        }
    }
    ring->features = params.features;
    ring->use_sqpoll = (params.flags & IORING_SETUP_SQPOLL) != 0;
    int sring_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned int);
    int cring_sz = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    if (params.features & IORING_FEAT_SINGLE_MMAP) {
        if (cring_sz > sring_sz) sring_sz = cring_sz;
        cring_sz = sring_sz;
    }
    int page_size = sysconf(_SC_PAGESIZE);
    sring_sz = (sring_sz + page_size - 1) & ~(page_size - 1);
    cring_sz = (cring_sz + page_size - 1) & ~(page_size - 1);
    ring->sq_ring_size = (size_t)sring_sz;
    ring->cq_ring_size = (size_t)cring_sz;
    void *sq_ring = mmap(0, (size_t)sring_sz, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_SQ_RING);
    if (sq_ring == MAP_FAILED) { close(ring->ring_fd); return -1; }
    ring->sq_ring_base = (char *)sq_ring;
    void *cq_ring;
    if (params.features & IORING_FEAT_SINGLE_MMAP) {
        cq_ring = sq_ring;
        ring->cq_ring_base = ring->sq_ring_base;
    } else {
        cq_ring = mmap(0, (size_t)cring_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_CQ_RING);
        if (cq_ring == MAP_FAILED) { munmap(sq_ring, (size_t)sring_sz); close(ring->ring_fd); return -1; }
        ring->cq_ring_base = (char *)cq_ring;
    }
    ring->shead = (unsigned int *)((char *)sq_ring + params.sq_off.head);
    ring->stail = (unsigned int *)((char *)sq_ring + params.sq_off.tail);
    ring->sring_mask = (unsigned int *)((char *)sq_ring + params.sq_off.ring_mask);
    ring->sring_entries = (unsigned int *)((char *)sq_ring + params.sq_off.ring_entries);
    ring->sqes_mask = *(ring->sring_mask);
    ring->sqes_entries = *(ring->sring_entries);
    size_t sqes_size = (size_t)params.sq_entries * sizeof(struct io_uring_sqe);
    sqes_size = (sqes_size + page_size - 1) & ~(page_size - 1);
    void *sqes = mmap(0, sqes_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_SQES);
    if (sqes == MAP_FAILED) {
        munmap(ring->sq_ring_base, (size_t)sring_sz);
        if (!(params.features & IORING_FEAT_SINGLE_MMAP)) munmap(ring->cq_ring_base, (size_t)cring_sz);
        close(ring->ring_fd);
        ring->sq_ring_base = NULL;
        ring->cq_ring_base = NULL;
        return -1;
    }
    ring->sq_sqes_base = (char *)sqes;
    ring->sq_sqes_size = sqes_size;
    ring->sqes = (struct io_uring_sqe *)sqes;
    ring->cring_mask = (unsigned int *)((char *)cq_ring + params.cq_off.ring_mask);
    ring->cring_entries = (unsigned int *)((char *)cq_ring + params.cq_off.ring_entries);
    ring->chead = (unsigned int *)((char *)cq_ring + params.cq_off.head);
    ring->ctail = (unsigned int *)((char *)cq_ring + params.cq_off.tail);
    ring->cqes = (struct io_uring_cqe *)((char *)cq_ring + params.cq_off.cqes);
    ring->sq_head = 0;
    ring->sq_tail = 0;
    ring->cached_cqe = 0;
    ring->pending_count = 0;
    return 0;
}

static void iouring_ring_destroy(struct io_ring *ring) {
    if (ring->ring_fd < 0) return;
    if (ring->sq_sqes_base && ring->sq_sqes_size > 0)
        munmap(ring->sq_sqes_base, ring->sq_sqes_size);
    if (ring->sq_ring_base && ring->sq_ring_size > 0)
        munmap(ring->sq_ring_base, ring->sq_ring_size);
    if (ring->cq_ring_base && ring->cq_ring_base != ring->sq_ring_base && ring->cq_ring_size > 0)
        munmap(ring->cq_ring_base, ring->cq_ring_size);
    close(ring->ring_fd);
    memset(ring, 0, sizeof(struct io_ring));
    ring->ring_fd = -1;
}

static inline int iouring_submit(struct io_ring *ring) {
    unsigned int tail = ring->sq_tail;
    __atomic_store_n(ring->stail, tail, __ATOMIC_RELEASE);
    int ret;
    if (ring->use_sqpoll) {
        ret = 1;
    } else {
        ret = (int)syscall(__NR_io_uring_enter, (long)ring->ring_fd,
                            (long)(tail - ring->sq_head), 0,
                            (unsigned int)IORING_ENTER_GETEVENTS, NULL, 0);
    }
    ring->sq_head = tail;
    return ret;
}

static inline int iouring_enter(struct io_ring *ring) {
    int ret = (int)syscall(__NR_io_uring_enter, (long)ring->ring_fd, 0,
                            (long)IOURING_MAX_CQES,
                            (unsigned int)IORING_ENTER_GETEVENTS, NULL, 0);
    return ret;
}

static inline struct io_uring_sqe *iouring_get_sqe(struct io_ring *ring) {
    unsigned int tail = ring->sq_tail;
    unsigned int next = tail + 1;
    if (next - ring->sq_head > ring->sqes_entries) return NULL;
    ring->sq_tail = next;
    return &ring->sqes[tail & ring->sqes_mask];
}

static inline int iouring_cq_ready(struct io_ring *ring) {
    return __atomic_load_n(ring->ctail, __ATOMIC_ACQUIRE) - ring->cached_cqe;
}

static inline struct io_uring_cqe *iouring_get_cqe(struct io_ring *ring) {
    unsigned int mask = *(ring->cring_mask);
    unsigned int tail = __atomic_load_n(ring->ctail, __ATOMIC_ACQUIRE);
    if (ring->cached_cqe == tail) return NULL;
    return &ring->cqes[ring->cached_cqe++ & mask];
}

static void iouring_cq_advance(struct io_ring *ring) {
    __atomic_store_n(ring->chead, ring->cached_cqe, __ATOMIC_RELEASE);
}

static int event_loop_add_conn(EventLoop *el, int fd) {
    if (el->conn_count >= el->conn_capacity) {
        int new_cap = el->conn_capacity == 0 ? 1024 : el->conn_capacity * 2;
        int *new_fds = realloc(el->conn_fds, sizeof(int) * (size_t)new_cap);
        if (!new_fds) return -1;
        el->conn_fds = new_fds;
        el->conn_capacity = new_cap;
    }
    el->conn_fds[el->conn_count++] = fd;
    return 0;
}

static void event_loop_remove_conn(EventLoop *el, int fd) {
    for (int i = 0; i < el->conn_count; i++) {
        if (el->conn_fds[i] == fd) {
            el->conn_fds[i] = el->conn_fds[el->conn_count - 1];
            el->conn_count--;
            return;
        }
    }
}

static int event_loop_accept_conn(EventLoop *el) {
    int accepted = 0;
    for (int j = 0; j < el->listener_count; j++) {
        int listen_fd = el->listener_fds[j];
        for (;;) {
            int client_fd = (int)accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                break;
            }
            if (el->use_io_uring) {
                event_loop_add_conn(el, client_fd);
                if (el->on_accept) el->on_accept(el, client_fd, el->userdata);
            } else {
                struct epoll_event cev = {.events = EPOLLIN | EPOLLET | EPOLLRDHUP, .data = {.fd = client_fd}};
                epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);
                event_loop_add_conn(el, client_fd);
                if (el->on_accept) el->on_accept(el, client_fd, el->userdata);
            }
            accepted++;
        }
    }
    return accepted;
}

EventLoop *event_loop_create(EventLoopConfig cfg) {
    EventLoop *el = calloc(1, sizeof(EventLoop));
    if (!el) return NULL;
    el->max_events = cfg.max_events > 0 ? cfg.max_events : MAX_EVENTS_DEFAULT;
    el->timeout_ms = cfg.timeout_ms >= 0 ? cfg.timeout_ms : 100;
    el->tcp_fastopen = cfg.tcp_fastopen;
    el->zero_copy = cfg.zero_copy;
    el->busy_poll = cfg.busy_poll;
    el->use_epoll = cfg.use_epoll;
    el->use_io_uring = cfg.use_io_uring && iouring_available();
    el->epoll_fd = epoll_create1(0);
    if (el->epoll_fd < 0) { free(el); return NULL; }
    el->event_fd = eventfd(0, EFD_NONBLOCK);
    if (el->event_fd < 0) { close(el->epoll_fd); free(el); return NULL; }
    struct epoll_event ev = {.events = EPOLLIN, .data = {.fd = el->event_fd}};
    epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, el->event_fd, &ev);
    pthread_mutex_init(&el->write_mutex, NULL);
    if (el->use_io_uring) {
        memset(&el->iou, 0, sizeof(IOUringData));
        int queue_depth = cfg.io_uring_queue_depth > 0 ? cfg.io_uring_queue_depth : IOURING_QUEUE_DEPTH;
        int use_sqpoll = cfg.io_uring_sqpoll;
        el->iou.use_io_uring = 1;
        if (iouring_ring_setup(&el->iou.ring, queue_depth, use_sqpoll) == 0) {
            el->iou.sqpoll_enabled = el->iou.ring.use_sqpoll;
        } else {
            el->iou.use_io_uring = 0;
            el->use_io_uring = 0;
        }
    }
    el->conn_fds = NULL;
    el->conn_count = 0;
    el->conn_capacity = 0;
    return el;
}

void event_loop_set_callbacks(EventLoop *el, accept_cb on_accept, read_cb on_read,
                               write_cb on_write, close_cb on_close, error_cb on_error, void *userdata) {
    if (!el) return;
    el->on_accept = on_accept;
    el->on_read = on_read;
    el->on_write = on_write;
    el->on_close = on_close;
    el->on_error = on_error;
    el->userdata = userdata;
}

int event_loop_add_fd(EventLoop *el, int fd, int events) {
    struct epoll_event ev;
    ev.events = events | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) return -1;
    return event_loop_add_conn(el, fd);
}

int event_loop_mod_fd(EventLoop *el, int fd, int events) {
    struct epoll_event ev;
    ev.events = events | EPOLLET;
    ev.data.fd = fd;
    return epoll_ctl(el->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

int event_loop_del_fd(EventLoop *el, int fd) {
    epoll_ctl(el->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    event_loop_remove_conn(el, fd);
    return 0;
}

int event_loop_add_listener(EventLoop *el, int fd) {
    struct epoll_event ev = {.events = EPOLLIN, .data = {.fd = fd}};
    if (epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) return -1;
    if (el->listener_count < 64) el->listener_fds[el->listener_count++] = fd;
    return 0;
}

int event_loop_add_signal_fd(EventLoop *el, int fd) {
    struct epoll_event ev = {.events = EPOLLIN, .data = {.fd = fd}};
    return epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static int iouring_handle_cqes(EventLoop *el) {
    int count = 0;
    struct io_uring_cqe *cqe;
    while ((cqe = iouring_get_cqe(&el->iou.ring)) != NULL) {
        count++;
        int fd = (int)(cqe->user_data >> 32);
        int op_type = (int)(cqe->user_data & 0xFFFFFFFF);
        int res = cqe->res;
        if (res < 0) {
            if (res == -EAGAIN || res == -EINTR) {
                goto skip;
            }
            if (op_type == IORING_OP_READ && el->on_close) {
                el->on_close(el, fd, el->userdata);
            } else if (op_type == IORING_OP_ACCEPT) {
            }
            goto skip;
        }
        switch (op_type) {
            case IORING_OP_ACCEPT:
                if (res > 0 && el->on_accept) {
                    el->on_accept(el, res, el->userdata);
                    event_loop_add_conn(el, res);
                }
                break;
            case IORING_OP_READ:
                if (el->on_read) {
                    int close_fd = el->on_read(el, fd, NULL, 0, el->userdata);
                    if (close_fd && el->on_close) {
                        el->on_close(el, fd, el->userdata);
                    }
                }
                break;
            case IORING_OP_WRITE:
                if (el->on_write) {
                    el->on_write(el, fd, el->userdata);
                }
                break;
            case IORING_OP_RECV:
                if (el->on_read) {
                    int close_fd = el->on_read(el, fd, NULL, (size_t)res, el->userdata);
                    if (close_fd && el->on_close) {
                        el->on_close(el, fd, el->userdata);
                    }
                }
                break;
            case IORING_OP_SEND:
                if (el->on_write) {
                    el->on_write(el, fd, el->userdata);
                }
                break;
        }
skip:
        (void)0;
    }
    iouring_cq_advance(&el->iou.ring);
    return count;
}

static int iouring_submit_accept(EventLoop *el) {
    int submitted = 0;
    for (int i = 0; i < el->listener_count; i++) {
        int listen_fd = el->listener_fds[i];
        struct io_uring_sqe *sqe = iouring_get_sqe(&el->iou.ring);
        if (!sqe) break;
        memset(sqe, 0, sizeof(*sqe));
        sqe->opcode = IORING_OP_ACCEPT;
        sqe->fd = listen_fd;
        sqe->addr = 0;
        sqe->addr2 = 0;
        sqe->accept_flags = SOCK_NONBLOCK | SOCK_CLOEXEC;
        sqe->user_data = ((uint64_t)listen_fd << 32) | IORING_OP_ACCEPT;
        submitted++;
    }
    return submitted;
}

static int iouring_process_epoll_fallback(EventLoop *el) {
    struct epoll_event events_buf[64];
    int nfds = epoll_wait(el->epoll_fd, events_buf, 64, 0);
    if (nfds < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    for (int i = 0; i < nfds; i++) {
        int fd = events_buf[i].data.fd;
        if (fd == el->event_fd) {
            uint64_t val;
            read(el->event_fd, &val, sizeof(val));
            continue;
        }
        int is_listener = 0;
        for (int j = 0; j < el->listener_count; j++) {
            if (el->listener_fds[j] == fd) { is_listener = 1; break; }
        }
        if (is_listener) continue;
        if (events_buf[i].events & (EPOLLHUP | EPOLLERR)) {
            if (el->on_close) el->on_close(el, fd, el->userdata);
            close(fd);
            event_loop_remove_conn(el, fd);
        } else if (events_buf[i].events & EPOLLIN) {
            if (el->on_read) {
                int close_fd = el->on_read(el, fd, NULL, 0, el->userdata);
                if (close_fd) {
                    if (el->on_close) el->on_close(el, fd, el->userdata);
                    close(fd);
                    event_loop_remove_conn(el, fd);
                }
            }
        } else if (events_buf[i].events & EPOLLOUT) {
            flush_write_queue(el);
            if (el->write_queue_count == 0) {
                struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data = {.fd = fd}};
                epoll_ctl(el->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
            }
            if (el->on_write) el->on_write(el, fd, el->userdata);
        }
    }
    return 0;
}

static int iouring_process_main(EventLoop *el, volatile sig_atomic_t *running) {
    struct io_ring *ring = &el->iou.ring;
    (void)running;
    int ret = event_loop_accept_conn(el);
    (void)ret;
    int nready = iouring_cq_ready(ring);
    if (nready > 0) {
        iouring_handle_cqes(el);
    }
    iouring_submit_accept(el);
    if (ring->sq_tail != ring->sq_head) {
        iouring_submit(ring);
    }
    iouring_process_epoll_fallback(el);
    if (ring->pending_count > 0 && nready == 0) {
        iouring_enter(ring);
        iouring_handle_cqes(el);
    }
    return 0;
}

int event_loop_write(EventLoop *el, int fd, const char *data, size_t len) {
    ssize_t sent = send(fd, data, len, MSG_NOSIGNAL);
    if (sent == (ssize_t)len) {
        if (el->on_write) el->on_write(el, fd, el->userdata);
        return 0;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pthread_mutex_lock(&el->write_mutex);
        WriteBuf *buf = malloc(sizeof(WriteBuf) + len);
        if (!buf) { pthread_mutex_unlock(&el->write_mutex); return -1; }
        buf->data = (char*)(buf + 1);
        memcpy(buf->data, data, len);
        buf->len = len;
        buf->sent = sent > 0 ? (size_t)sent : 0;
        buf->fd = fd;
        buf->is_sendfile = 0;
        buf->next = NULL;
        WriteBuf *tail = &el->write_queue;
        while (tail->next) tail = tail->next;
        tail->next = buf;
        el->write_queue_count++;
        pthread_mutex_unlock(&el->write_mutex);
        struct epoll_event ev = {.events = EPOLLIN | EPOLLOUT | EPOLLET, .data = {.fd = fd}};
        epoll_ctl(el->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
        return 0;
    }
    return -1;
}

int event_loop_sendfile(EventLoop *el, int fd, int file_fd, off_t offset, size_t count) {
    if (el->zero_copy) {
        ssize_t sent = sendfile(fd, file_fd, &offset, count);
        if (sent > 0) {
            if (el->on_write) el->on_write(el, fd, el->userdata);
            return 0;
        }
        return sent < 0 ? -1 : 0;
    }
    char buf[65536];
    ssize_t n;
    lseek(file_fd, offset, SEEK_SET);
    while (count > 0) {
        size_t to_read = count > sizeof(buf) ? sizeof(buf) : count;
        n = read(file_fd, buf, to_read);
        if (n <= 0) break;
        if (event_loop_write(el, fd, buf, (size_t)n) < 0) return -1;
        count -= (size_t)n;
    }
    return 0;
}

int event_loop_splice(EventLoop *el, int fd_in, int fd_out, size_t len) {
    int p[2];
    if (pipe(p) < 0) return -1;
    ssize_t n = splice(fd_in, NULL, p[1], NULL, len, SPLICE_F_MOVE | SPLICE_F_MORE);
    if (n <= 0) { close(p[0]); close(p[1]); return -1; }
    (void)n;
    int ret = event_loop_write(el, fd_out, NULL, 0);
    close(p[0]); close(p[1]);
    (void)ret;
    return 0;
}

void flush_write_queue(EventLoop *el) {
    pthread_mutex_lock(&el->write_mutex);
    WriteBuf *prev = &el->write_queue;
    WriteBuf *buf = prev->next;
    while (buf) {
        ssize_t sent = send(buf->fd, buf->data + buf->sent, buf->len - buf->sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent > 0) buf->sent += (size_t)sent;
        if (buf->sent >= buf->len) {
            prev->next = buf->next;
            free(buf);
            buf = prev->next;
            el->write_queue_count--;
        } else {
            if (sent < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                prev->next = buf->next;
                if (el->on_close) el->on_close(el, buf->fd, el->userdata);
                free(buf);
                buf = prev->next;
                el->write_queue_count--;
            } else { prev = buf; buf = buf->next; }
        }
    }
    pthread_mutex_unlock(&el->write_mutex);
}

int event_loop_run(EventLoop *el, volatile sig_atomic_t *running) {
    if (!el) return -1;
    if (el->use_io_uring) {
        return iouring_process_main(el, running);
    }
    struct epoll_event *events = NULL;
    int max_events = el->max_events > 4096 ? 4096 : el->max_events;
    events = malloc(sizeof(struct epoll_event) * (size_t)max_events);
    if (!events) return -1;
    int nfds = epoll_wait(el->epoll_fd, events, max_events, el->timeout_ms);
    if (nfds < 0) {
        if (errno == EINTR) { free(events); return 0; }
        free(events);
        return -1;
    }
    for (int i = 0; i < nfds; i++) {
        int fd = events[i].data.fd;
        if (fd == el->event_fd) {
            uint64_t val;
            read(el->event_fd, &val, sizeof(val));
            continue;
        }
        int is_listener = 0;
        for (int j = 0; j < el->listener_count; j++) {
            if (el->listener_fds[j] == fd) { is_listener = 1; break; }
        }
        if (is_listener) {
            while (*running) {
                int client_fd = (int)accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    continue;
                }
                struct epoll_event cev = {.events = EPOLLIN | EPOLLET | EPOLLRDHUP, .data = {.fd = client_fd}};
                epoll_ctl(el->epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);
                event_loop_add_conn(el, client_fd);
                if (el->on_accept) el->on_accept(el, client_fd, el->userdata);
            }
        } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
            if (el->on_close) el->on_close(el, fd, el->userdata);
            close(fd);
            event_loop_remove_conn(el, fd);
        } else if (events[i].events & EPOLLIN) {
            int close_fd = 0;
            if (el->on_read) close_fd = el->on_read(el, fd, NULL, 0, el->userdata);
            if (close_fd) {
                if (el->on_close) el->on_close(el, fd, el->userdata);
                close(fd);
                event_loop_remove_conn(el, fd);
            }
        } else if (events[i].events & EPOLLOUT) {
            flush_write_queue(el);
            if (el->write_queue_count == 0) {
                struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data = {.fd = fd}};
                epoll_ctl(el->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
            }
            if (el->on_write) el->on_write(el, fd, el->userdata);
        }
    }
    free(events);
    return 0;
}

void event_loop_stop(EventLoop *el) {
    if (!el) return;
    uint64_t val = 1;
    write(el->event_fd, &val, sizeof(val));
}

void event_loop_destroy(EventLoop *el) {
    if (!el) return;
    if (el->iou.use_io_uring) {
        iouring_ring_destroy(&el->iou.ring);
    }
    close(el->epoll_fd);
    close(el->event_fd);
    free(el->conn_fds);
    pthread_mutex_lock(&el->write_mutex);
    WriteBuf *buf = el->write_queue.next;
    while (buf) { WriteBuf *next = buf->next; free(buf); buf = next; }
    pthread_mutex_unlock(&el->write_mutex);
    pthread_mutex_destroy(&el->write_mutex);
    free(el);
}
