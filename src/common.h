#ifndef AIONIC_COMMON_H
#define AIONIC_COMMON_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

// ===== C Standard Library =====
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

// ===== POSIX / System =====
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

// ===== Networking =====
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

// ===== Dynamic Linking =====
#include <dlfcn.h>

#endif // AIONIC_COMMON_H

