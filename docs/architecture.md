# NeuroHTTP Architecture Overview

This document describes the internal architecture and security design of NeuroHTTP.
Parts of this documentation were validated and summarized using automated
code analysis tools (e.g., zread.ai), then reviewed and refined by the author.

# Overview

NeuroHTTP is a high-performance, AI-native web server built from scratch in C and Assembly, designed specifically to handle heavy AI payloads with minimal latency. Unlike traditional web servers optimized for static content, NeuroHTTP is engineered for dynamic AI workloads, featuring an innovative prompt routing system, hardware-accelerated processing, and comprehensive security layers.


# Core Architecture

The system is built around a modular architecture that combines low-level systems programming with modern AI integration capabilities. At its foundation, NeuroHTTP uses an epoll-based event loop model for efficient connection handling, complemented by a configurable thread pool for parallel processing. The server version 1.0.0, codenamed "AIONIC," implements a non-blocking I/O model that scales gracefully under heavy concurrent loads while maintaining predictable latency characteristics.

Sources: src/main.c, include/server.h

## System Architecture Flow
## Key Features
## AI-Native Prompt Routing

NeuroHTTP includes a sophisticated prompt routing system that manages multiple AI model endpoints dynamically. The prompt router supports model registration, availability management, and intelligent routing based on model capabilities and configuration. Users can configure multiple AI providers including OpenAI-compatible APIs, Groq, or local models, with the system automatically handling HTTP requests and JSON parsing internally. The router supports both default model selection and explicit model targeting through API parameters, making it provider-agnostic and highly flexible for different deployment scenarios.

Sources: include/ai/prompt_router.h

# Hardware-Accelerated Processing

One of NeuroHTTP's standout features is its extensive use of assembly optimizations for critical performance paths. The server automatically detects CPU capabilities at startup and selects the appropriate implementation based on available hardware features. This includes:

* CRC32 checksum calculations with SSE4.2 and AVX2 implementations
* Fast JSON parsing through assembly-optimized routines
* Optimized memory operations using SIMD instructions

The system validates hardware acceleration support during initialization, testing assembly linkage and ensuring consistency between different implementations. This hardware-aware architecture provides significant performance benefits for computationally intensive operations like AI payload processing and network data handling.

Sources: src/main.c, src/asm/crc32.s

## High-Performance Routing

The routing system employs a hash table-based approach with O(1) average lookup performance, supporting dynamic route registration and parameter extraction. Routes can be registered with specific HTTP methods (GET, POST, PUT, DELETE, etc.) and include parameter handling capabilities for RESTful path patterns. The router also supports middleware functions that can intercept and process requests before they reach their final handlers, enabling cross-cutting concerns like authentication, logging, or request transformation. The system includes thread-safe route table management with read-write locks to support concurrent access in multi-threaded environments.

Sources: include/router.h

## Streaming Response System

For AI workloads where responses can be large or need to be delivered incrementally, NeuroHTTP implements a comprehensive streaming system with chunked transfer encoding support. The streaming architecture includes both network and buffer stream implementations, with configurable buffer sizes, timeout handling, and non-blocking operation modes. Enhanced features include callback mechanisms for data and error events, priority-based queuing, and detailed statistics collection for monitoring performance. The system supports both basic streaming for simple use cases and extended streaming with advanced configuration options for complex scenarios.

Sources: include/stream.h

## Integrated Security Layer

NeuroHTTP incorporates a sophisticated Web Application Firewall (WAF) with multiple protection mechanisms:

* Rate limiting with configurable thresholds and automatic blocking
* Brute force detection based on repeated failed authentication attempts
* Attack pattern detection for SQL injection and XSS vulnerabilities
* Suspicious activity scoring with configurable thresholds
* IP whitelisting and blacklisting with permanent and time-based rules
* Detailed security statistics and reporting

The firewall provides both legacy support for basic request checking and enhanced checking that includes request body analysis and user-agent inspection for scanner detection. Security rules can be dynamically configured at runtime without restarting the server.

Sources: include/firewall.h

## Real-Time Optimization Engine

The system includes a continuous optimization engine that monitors performance metrics and automatically adjusts server parameters for optimal throughput. The optimizer collects real-time statistics including request counts, response times, error rates, and resource utilization, then applies dynamic tuning to control levers like connection limits, buffer sizes, and thread pool allocation. This self-tuning capability ensures the server maintains peak performance across varying load patterns and deployment environments.

Sources: include/optimizer.h, src/main.c

# Performance Characteristics

NeuroHTTP demonstrates distinctive performance characteristics optimized for AI workloads. Benchmarks against NGINX 1.29.3 reveal that while NGINX achieves higher raw request throughput (approximately 8,000 req/s vs NeuroHTTP's 2,600 req/s), NeuroHTTP delivers significantly lower latency (57-63ms average vs 114-117ms for NGINX) and transfers 6× more data per second (7.9 MB/s vs 1.2 MB/s). This profile indicates NeuroHTTP is optimized for heavier, data-rich AI responses rather than lightweight static content serving.

Sources: benchmark.md

## Performance Comparison

The following benchmark compares **NeuroHTTP (C + Assembly optimizations)** against **NGINX 1.29.3** under high concurrency scenarios.  
Tests were conducted using sustained connection loads and identical networking conditions.

| Metric | NeuroHTTP (C + ASM) | NGINX 1.29.3 | Advantage |
|------|--------------------|--------------|-----------|
| Avg Latency (10K connections) | 57.32 ms | 114.95 ms | **NeuroHTTP: ~50% lower** |
| Avg Latency (40K connections) | 63.35 ms | 116.88 ms | **NeuroHTTP: ~46% lower** |
| Data Transfer Rate | 7.9 MB/s | 1.2 MB/s | **NeuroHTTP: ~6× higher** |
| Request Throughput | ~2,590 req/s | ~8,148 req/s | **NGINX: ~3× higher** |
| Latency Stability | Tight distribution | More variable | **NeuroHTTP: More predictable** |

> **Notes**
> - NeuroHTTP prioritizes *low and stable latency* through a minimal event loop, zero-copy paths, and assembly-level optimizations.
> - NGINX achieves higher raw throughput due to aggressive buffering and mature worker scaling.
> - The results highlight different design goals rather than a direct replacement scenario.

---

## Project Structure

The NeuroHTTP codebase is organized around a clear separation of concerns, with performance-critical paths optimized at both C and assembly levels.

NeuroHTTP/
├── include/
│ ├── ai/ # AI routing and model management
│ ├── server.h # Core server structures and public API
│ ├── router.h # Hash table–based routing system
│ ├── firewall.h # Firewall, WAF, and security primitives
│ └── stream.h # Streaming response subsystem
│
├── src/ # Core implementation
│ ├── main.c # Server entry point and initialization
│ ├── server.c # Event loop, epoll handling, thread pool
│ ├── router.c # Request dispatch and routing logic
│ ├── firewall.c # Security rules and IP reputation logic
│ └── ai/ # AI prompt routing implementation
│
├── src/asm/ # Assembly-level optimizations
│ ├── crc32.s # SSE4.2 / AVX2 accelerated checksums
│ ├── json_fast.s # High-performance JSON parsing
│ └── memcpy_asm.s # Optimized memory copy routines
│
├── plugins/ # Extensible plugin system
│ ├── limiter.c # Rate limiting plugin
│ ├── logstats.c # Metrics and statistics logging
│ └── openai_proxy.c # OpenAI-compatible API proxy
│
├── config/ # Configuration files
│ └── aionic.conf # Server and security configuration
│
├── tests/ # Test suites and validation tools
└── docs/ # Architecture and security documentation

## Quick Reference

### Configuration Options

NeuroHTTP provides a concise and flexible configuration system for performance tuning and feature enablement.  
The following table summarizes the most commonly used parameters:

| Parameter | Description | Default |
|---------|------------|---------|
| `port` | Server listening port | `8080` |
| `thread_count` | Number of worker threads | Auto-detected |
| `max_connections` | Maximum concurrent connections | `10000` |
| `enable_cache` | Enable response caching | `1` |
| `enable_firewall` | Enable WAF and security protections | `1` |
| `enable_optimization` | Enable automatic performance tuning | `1` |

> Configuration definitions are located in `include/config.h`.

---

### API Endpoints

NeuroHTTP exposes several built-in HTTP endpoints for server management, monitoring, and AI interaction:

| Method | Endpoint | Description |
|------|----------|-------------|
| `POST` | `/v1/chat` | Send prompts to AI models with streaming responses |
| `GET` | `/stats` | Retrieve real-time server statistics and performance metrics |
| `GET` | `/health` | Health check endpoint for monitoring systems |
| `GET` | `/` | Root endpoint returning server information |

---

### Learning Path

For developers new to **NeuroHTTP**, the following documentation flow is recommended:

1. **Quick Start** — Run the server in minutes
2. **Building from Source** — Compilation steps and dependencies
3. **Configuration Setup** — Server tuning and feature flags
4. **Architecture Overview** — System design and internal components
5. **Request Lifecycle Flow** — End-to-end request processing

Developers interested in specific subsystems can explore the **Deep Dive** documentation, which covers:

- Event loop and concurrency model  
- Assembly-level optimizations  
- AI routing and model integration  
- Request routing strategies  
- Firewall, WAF, and security mechanisms  

This structure allows both new users and advanced contributors to quickly navigate the codebase and understand the system.

---

## Architecture Overview

NeuroHTTP is a **high-performance, AI-native web server** built from scratch in **C and Assembly**.  
The architecture is designed around three core principles:

- **Minimal latency**
- **Hardware acceleration**
- **AI-first integration**

This document provides an overview of the system architecture, component interactions, and the key design decisions that enable NeuroHTTP to handle AI-heavy workloads with performance characteristics that differ significantly from traditional web servers such as NGINX.

> Sources: `main.c`, `server.h`

---

## System Architecture

NeuroHTTP follows a **layered, event-driven architecture** optimized for AI workloads.  
Each layer addresses a specific concern while maintaining clear separation of responsibilities and high internal cohesion.

┌─────────────────────────────────────────────────────────────────┐
│ Application Layer │
│ ┌─────────────┐ ┌──────────────┐ ┌────────────────────┐ │
│ │ AI Layer │ │ Router │ │ Plugin System │ │
│ │ (Prompt │ │ (Hash table │ │ (Dynamic loading) │ │
│ │ Routing) │ │ + params) │ │ │ │
│ └─────────────┘ └──────────────┘ └────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ Middleware Layer │
│ ┌─────────────┐ ┌──────────────┐ ┌────────────────────┐ │
│ │ Firewall │ │ Optimizer │ │ Cache │ │
│ │ (WAF + │ │ (Auto-tune) │ │ (TTL-based) │ │
│ │ Rate Limit)│ │ │ │ │ │
│ └─────────────┘ └──────────────┘ └────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ Core Server Layer │
│ ┌─────────────┐ ┌──────────────┐ ┌────────────────────┐ │
│ │ Event Loop │ │ Thread Pool │ │ Connection Pool │ │
│ │ (epoll) │ │ (Workers) │ │ (State tracking) │ │
│ └─────────────┘ └──────────────┘ └────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│ Hardware Acceleration Layer │
│ ┌─────────────┐ ┌──────────────┐ ┌────────────────────┐ │
│ │ CRC32 │ │ JSON Parser │ │ Memory Operations │ │
│ │ (SSE4.2 / │ │ (Fast ASM) │ │ (AVX2 / AVX-512) │ │
│ │ AVX2) │ │ │ │ │ │
│ └─────────────┘ └──────────────┘ └────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘


> Sources: `server.c`, `router.c`, `asm_utils.h`

---

## Request Flow Architecture

The request lifecycle illustrates how NeuroHTTP coordinates its internal components to process incoming requests with minimal overhead.

At a high level, each request follows this path:

1. **Connection acceptance** via the epoll-based event loop  
2. **Thread dispatch** to a worker from the thread pool  
3. **Firewall pre-filtering** (whitelist, blacklist, rate limiting, WAF)  
4. **Routing resolution** using hash-based path matching  
5. **AI prompt handling or plugin execution**, if applicable  
6. **Optional caching and optimization adjustments**  
7. **Streaming or buffered response delivery**  

Understanding this flow is essential for grasping NeuroHTTP’s performance characteristics, particularly its ability to maintain **low and stable latency** under AI-heavy workloads.

![NeuroHTTP Architecture Diagram](video/arch.png)

**Source files:** `server.c`, `firewall.h`

---

## Core Server Components

### Event Loop & epoll Implementation

NeuroHTTP employs **Linux epoll** for high-performance I/O multiplexing, allowing the server to handle thousands of concurrent connections with minimal resource overhead.  
The event loop architecture is implemented in `server.c` with the following characteristics:

- **Single-acceptor model:** One dedicated thread accepts new connections and distributes them to worker threads  
- **Edge-triggered epoll:** Reduces system call overhead by notifying only on state changes  
- **Connection tracking:** Maintains detailed connection metadata for debugging and statistics  
- **Non-blocking sockets:** All operations are non-blocking to prevent thread starvation  

The server maintains an array of epoll file descriptors (`server.h`) for efficient event distribution across multiple threads, enabling horizontal scaling across CPU cores.

*Sources: `server.c`, `server.h`*

---

### Thread Pool Architecture

The thread pool provides a balanced approach to concurrency management:

| Component        | Description                                  | Configuration                 |
|-----------------|----------------------------------------------|-------------------------------|
| Worker Threads   | 4 threads by default, configurable          | `thread_count` in `aionic.conf` |
| Thread Data      | Per-thread connection tracking               | `ThreadData` struct           |
| Work Distribution| Lock-free queue for high throughput          | Request queue abstraction     |
| Load Balancing   | Dynamic based on connection count           | Active connection tracking    |

Each worker thread runs an **infinite loop** processing events from its epoll instance, ensuring **CPU cache locality** and minimizing **lock contention**.  
The thread pool is initialized during server startup (`server.c`) and cleanly shut down during graceful termination.

*Sources: `ai/prompt_router.h`, `main.c`*

---

### Connection Management

The server maintains sophisticated connection tracking using the `ConnectionInfo` structure:

```c
typedef struct {
    int fd;
    char ip_address[INET6_ADDRSTRLEN];
    time_t connect_time;
    time_t last_activity;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    int request_count;
} ConnectionInfo;

```
This enables granular monitoring of each connection's lifecycle, supporting features such as:

Idle timeout detection

Bandwidth tracking

Connection-based rate limiting

The connection pool automatically expands and contracts based on load, respecting the max_connections limit (default: 1024).

Sources: server.c, server.h

AI Integration Layer
Prompt Router Architecture

The AI layer is built around the Prompt Router (ai/prompt_router.h), which provides a flexible abstraction for communicating with multiple AI backends:

![NeuroHTTP Architecture Diagram](video/0.png)

* Supports dynamic routing to different models

* Handles concurrent AI requests efficiently

* Integrates seamlessly with the server's event loop and thread pool

The prompt router uses libcurl for HTTP communication with AI backends, enabling support for any OpenAI-compatible API. The OPENAI_API_KEY environment variable can be configured to use different providers without code changes.

*Sources: `ai/prompt_router.h`, `main.c`*

---

## AI Communication Flow

NeuroHTTP follows a **synchronous request-response pattern** optimized for the typically long-running nature of AI inference:

1. **Prompt Processing:** Extract and validate the prompt from the HTTP request body  
2. **Model Selection:** Choose the appropriate AI model based on request metadata or default configuration  
3. **Tokenization:** Count input tokens using the integrated tokenizer  
4. **API Communication:** Send HTTP request to the AI backend via `libcurl`  
5. **Response Parsing:** Parse JSON response and extract the generated text  
6. **Response Caching:** Store the response in cache for identical future requests  

This architecture balances **flexibility with performance**, allowing seamless integration of new AI models while maintaining low latency for repeated prompts.

*Sources: `ai/prompt_router.h`, `router.c`*

---

## Routing & Middleware System

### Hash Table-Based Routing

The router uses a custom **hash table implementation** (`router.c`) for O(1) route lookups:

| Feature | Implementation | Performance Benefit |
|---------|----------------|-------------------|
| Hash Function | `hash_string()` with modulo 256 | Fast route lookup |
| Collision Resolution | Chained linked lists | Handles hash collisions |
| Parameter Extraction | Pattern matching with regex | Dynamic route parameters |
| Middleware Pipeline | Array of function pointers | Pre/post-request hooks |

- Routes are registered with path patterns like `/v1/chat` or `/users/:id`  
- `:id` represents a dynamic parameter automatically extracted for handlers  
- Each route entry (`router.h`) stores path, HTTP method, handler function, parameter count, and parameter names  

*Sources: `router.h`, `router.c`*

---

### Middleware Pipeline

The middleware system provides **flexible interception** for cross-cutting concerns:

```c
typedef int (*MiddlewareFunc)(HTTPRequest *, RouteResponse *);

```
Middleware functions are registered via **register_middleware()**

Executed in the order registered

Can inspect/modify requests, short-circuit processing, or modify responses

Built-in middleware includes: security checks, logging, compression

Custom middleware can be added via the plugin system

*Sources: `router.h`, `router.c`*

Security & Firewall System
WAF Attack Pattern Detection

NeuroHTTP includes a Web Application Firewall (WAF) with multiple detection layers:

Detection Mechanism	Description	Config Parameter
Rate Limiting	Requests per minute per IP	max_requests_per_minute
Brute Force Detection	Repeated failed auth attempts	brute_force_threshold
Attack Pattern Matching	SQLi, XSS, command injection	Custom patterns
Suspicious Score	Behavior-based scoring	suspicious_threshold
IP Whitelisting	Trust specific IPs	Permanent / Temporary
IP Blacklisting	Block known malicious IPs	With expiration times

Firewall maintains an in-memory hash table of IPs, request counts, block status, and suspicious scores

Expired entries are automatically cleaned up

Sources: firewall.h, server.c

Multi-Layer Security Model

Security checks occur at multiple points in the request lifecycle:

Connection Level: IP whitelist/blacklist check upon connection

Request Level: Rate limiting & brute force detection before parsing

Payload Level: Attack pattern detection on body & headers

Application Level: API key validation & suspicious activity scoring

Defense-in-depth approach ensures protection while maintaining performance via efficient data structures and minimal lock contention.

*Sources: `firewall.h`, `server.c`*

Hardware Acceleration
CPU Feature Detection

NeuroHTTP detects CPU features at runtime (asm_utils.h) to optimize performance:

```c
typedef struct {
    uint8_t sse42;
    uint8_t avx2;
    uint8_t avx512;
} cpu_features_t;

```

Automatically selects the best instruction set available

Falls back to scalar C implementations if advanced instructions are unavailable

Detection occurs at startup via CPUID instructions

Sources: asm_utils.h

Assembly Optimizations

Hand-written assembly is used for performance-critical operations:

Operation	SSE4.2	AVX2	AVX512
CRC32 Checksum	✓	✓	✗
JSON Tokenization	✗	✓	✗
Memory Copy	✗	✓	✓
String Operations	✗	Planned	Planned

Optimizations provide 2–10× speedups

Code in src/asm/ optimized for cache locality, instruction pipelining, and branch prediction

CRC32 used extensively for data integrity, hash computation, and checksums

*Sources: `asm_utils.h`, `Makefile`*

Performance Optimization Engine
Real-Time Optimizer

The optimizer (optimizer.h) monitors system performance and auto-tunes parameters:

```c
typedef struct {
    time_t timestamp;
    double cpu_usage;
    double memory_usage;
    uint64_t requests_per_second;
    double avg_response_time;
    int active_connections;
    int thread_pool_utilization;
} PerformanceData;

```


Capabilities:

Adjust thread pool size based on CPU utilization

Modify cache size dynamically

Tune network buffers based on throughput

Reconfigure rate limits automatically

Sources: optimizer.h

Response Caching Strategy

NeuroHTTP uses a TTL-based LRU cache (cache.h) for frequently accessed responses:

Feature	Implementation	Benefit
TTL Support	Per-entry expiration	Automatic invalidation
Access Tracking	access_count per entry	LRU eviction candidate
Size Limiting	Configurable max entries	Memory control
Hit/Miss Stats	Global counters	Performance monitoring

Cache is especially effective for AI responses

Default: 1000 entries, TTL = 3600 seconds (1 hour)

Sources: cache.h, config/aionic.conf

Configuration & Extensibility
Hierarchical Configuration

Text-based configuration with hot-reload support (aionic.conf):

```c
port = 8080
thread_count = 4
max_connections = 1024
request_timeout = 30000
buffer_size = 8192

```
# Cache Settings

```c
enable_cache = 1
cache_size = 1000
cache_ttl = 3600

```
# Security
```c
enable_firewall = 1
enable_optimization = 1

```

**Supports environment-specific overrides**

**Changes detected via inotify and reloaded without restart**

*Sources: `config/aionic.conf`, `main.c`*

Plugin System

Dynamic extension of server functionality (plugin.h):

Plugin	 Purpose	Description
limiter	 Request         limiting	        Custom rate-limiting rules
logstats	      Statistics logging	Export metrics externally
openai_proxy	     OpenAI compatibility     OpenAI API compatibility layer

Plugins loaded as .so shared libraries

Can register routes, middleware, and event handlers

Hooks for initialization, request processing, and cleanup

Sources: plugin.h, plugins/

Performance Comparison

NeuroHTTP demonstrates superior performance for AI-native workloads compared to traditional web servers:

(Benchmarks to be included here as in previous Performance Comparison section)

![NeuroHTTP Architecture Diagram](videos/neurohttp_2.png)

---

## Performance Overview

The performance advantage of NeuroHTTP stems from its **specialized design for AI workloads**:

- Optimized **JSON parsing**  
- Efficient **memory handling**  
- Reduced **context switching**  

While **NGINX** excels at serving static content, NeuroHTTP is optimized for **dynamic, payload-heavy AI inference requests**.

*Sources: `README.md`, `benchmark.md`*

---

## Build System

The build system (`Makefile`) uses **aggressive compiler optimizations** for maximum performance:

```makefile
CFLAGS = -Wall -Wextra -std=c11 -O3 -march=native -mtune=native -flto

```
Key Build Targets
Target	Description
make rebuild	Clean build with full optimization
make debug	Debug build with symbols, no optimization
make plugins	Build plugin shared libraries (.so files)
make tests	Build and run test suite
make install	Install system-wide with configuration

Supports Link Time Optimization (LTO) for cross-module optimization

Assembly files compiled with NASM for maximum control over generated machine code

Sources: Makefile

Next Steps

Now that the overall architecture is understood, explore these components in depth:

Event Loop & epoll Implementation – Learn about the event-driven I/O model

Hardware Acceleration Detection – Discover hardware-specific optimizations

Prompt Router Architecture – Understand AI integration

WAF Attack Pattern Detection – Explore security mechanisms

Real-Time Optimizer Engine – Dive into performance tuning

For practical guidance:

Quick Start – Build and run the server quickly

Building from Source – Detailed compilation instructions

---

# Event Loop & epoll Implementation

The NeuroHTTP server implements a sophisticated multi-threaded epoll-based event loop architecture designed for high-performance asynchronous I/O operations. This implementation leverages Linux's epoll system call for efficient event notification across multiple worker threads, each maintaining its own epoll instance for optimal CPU utilization and cache locality.

## Architecture Overview
The event loop architecture follows a producer-consumer pattern with a main thread accepting connections and distributing them across worker threads. Each worker thread operates its own epoll instance using edge-triggered mode, allowing the server to scale efficiently across multiple CPU cores while maintaining low latency.

![NeuroHTTP Architecture Diagram](videos/arch2.png)

The Server structure maintains an array of epoll file descriptors (epoll_fds), one per worker thread, enabling parallel event processing across threads.

*Sources: `include/server.h`, ` src/server.c`*

#Event Loop Architecture
##Main Thread Event Processing

The main thread continuously executes server_process_events() in the main event loop, responsible for accepting new connections and distributing them to worker threads. This function uses a non-blocking accept pattern with a configurable maximum connection limit.

The main thread performs several critical operations: accepting new client connections, setting sockets to non-blocking mode, checking firewall blacklists, and tracking connection metadata. Each new connection is distributed using round-robin assignment to worker threads based on modulo arithmetic of the active connection count.

Sources: src/server.c, src/main.c

##Worker Thread Event Loop

Each worker thread executes an infinite loop centered around epoll_wait(), blocking for up to 100 milliseconds for events on its assigned epoll instance. The worker threads are created during server_start() with dedicated epoll instances created via epoll_create1(0).

The worker thread event processing logic handles three distinct event types: EPOLLIN for incoming data ready to read, EPOLLHUP for connection hangup, and EPOLLERR for connection errors. This separation ensures proper connection cleanup and resource management.

Sources: src/server.c, src/server.c

##epoll Implementation Details
###Edge-Triggered Mode Configuration

NeuroHTTP employs edge-triggered epoll mode (EPOLLET) rather than the default level-triggered mode, requiring careful socket state management. When accepting new connections, the server configures epoll events with:

```c
struct epoll_event event;
event.events = EPOLLIN | EPOLLET;  // Edge-triggered mode
event.data.fd = client_fd;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);

```
Edge-triggered mode requires applications to read/write all available data on a file descriptor until EAGAIN or EWOULDBLOCK is returned, which the implementation handles through the streaming module's buffer management.

Sources: src/server.c

##Multi-Epoll Instance Design

The server creates a separate epoll instance for each worker thread, stored in the epoll_fds array within the Server structure. This design provides several advantages

## Performance Characteristics

### Thread Performance Advantages

NeuroHTTP's thread design provides the following advantages:

| Advantage                | Benefit |
|---------------------------|---------|
| CPU Cache Locality        | Each thread operates on its own data structures |
| Lock Contention Reduction | No shared epoll instance requiring synchronization |
| Scalability               | Linear scaling with CPU core count |
| Fair Load Distribution    | Round-robin connection assignment |

- The number of worker threads is configurable via the `thread_count` parameter in the server configuration, allowing operators to tune the server for specific hardware.

*Sources: `include/server.h`, `src/server.c`*

---

### Connection Lifecycle

#### Connection Acceptance Flow

The connection acceptance process integrates **firewall checks** and **connection tracking** before epoll registration:

1. Non-blocking socket creation via `accept()` with `SOCK_NONBLOCK`  
2. IP address extraction using `inet_ntop()`  
3. Firewall blacklist verification via `firewall_is_blacklisted()`  
4. Connection tracking metadata allocation via `add_connection_info()`  
5. Round-robin thread selection for epoll instance assignment  
6. epoll registration with `epoll_ctl(EPOLL_CTL_ADD)`  
7. Increment active connection counter  

> The firewall integration ensures malicious connections are rejected early, before consuming epoll resources.

*Sources: `src/server.c`*

---

#### Connection Termination

Connections are terminated via explicit error handling or peer-initiated closure. Worker threads perform:

- epoll descriptor removal via `epoll_ctl(EPOLL_CTL_DEL)`  
- Socket file descriptor closure  
- Active connection counter decrement  
- Connection tracking metadata removal  
- Closure logging for operational visibility  

> Comprehensive cleanup prevents resource leaks and ensures accurate connection statistics.

*Sources: `src/server.c`*

---

### Integration with Streaming Module

The **event loop** interfaces with the **streaming module** for efficient data transfer:

- Event loop manages connection state and I/O readiness  
- Streaming module handles actual data transmission with timeout management and error handling  
- Non-blocking send operations with configurable timeouts (`send()` with `MSG_DONTWAIT`)  
- Automatic retry logic with **exponential backoff** for transient network issues  
- Nanosecond-precision timeout handling using `clock_gettime(CLOCK_MONOTONIC)` and `nanosleep()`  

*Sources: `src/stream.c`, `include/stream.h`*

---

### Scalability Design

The **multi-epoll architecture** enables linear scaling with CPU core count:

- `MAX_EVENTS 1024` – Max events per `epoll_wait` iteration, limiting latency spikes  
- `BACKLOG 128` – Socket listen queue depth for pending connections  
- `100ms timeout` – Balances responsiveness and CPU utilization during idle periods  
- Connection tracking – Mutex-protected metadata ensures **thread-safe statistics**  
- Round-robin distribution – Ensures **even load** across worker threads  

*Sources: `src/server.c`*

---

### Resource Management

NeuroHTTP implements **comprehensive resource management**:

- Connection limits  
- Automatic cleanup of connection tracking metadata  
- Graceful shutdown handling: main thread signals worker threads via the `running` flag, joins threads, closes epoll instances  
- Connection metadata includes: IP address, timestamp, byte counters, request counters, suspicious activity flags (used by firewall and optimizer)

*Sources: `include/server.h`, `src/server.c`*

---

### Next Steps

The **event loop & epoll implementation** forms the foundation of NeuroHTTP's high-performance architecture.  

To explore further:

1. **Connection Management** – Understand how connections are managed throughout their lifecycle  
2. **Thread Pool Architecture** – Learn how the thread pool coordinates with the event loop  
3. **Request Lifecycle Flow** – Trace requests from acceptance to response completion


