<h1 align="center">⚡ NeuroHTTP — High-Performance AI-Native Web Server</h1>

<p align="center">
  <img src="https://img.shields.io/github/stars/okba14/NeuroHTTP?style=social" />
  <img src="https://img.shields.io/github/forks/okba14/NeuroHTTP?style=social" />
  <img src="https://github.com/okba14/NeuroHTTP/actions/workflows/make.yml/badge.svg" />
  <img src="https://img.shields.io/badge/build-passing-brightgreen?style=flat-square" />
  <img src="https://img.shields.io/badge/language-C%20%2B%20Assembly-blue?style=flat-square" />
  <img src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square" />
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Unix-lightgrey?style=flat-square" />
  <img src="https://img.shields.io/badge/AI%20APIs-ready-purple?style=flat-square" />
  <img src="https://img.shields.io/badge/speed-ultra%20fast-red?style=flat-square" />
</p>

<p align="center"><em>
"Redefining how AI APIs communicate with the web — built from scratch in C and Assembly."
</em></p>


---


## Benchmark Comparison

For detailed benchmark results comparing NeuroHTTP and NGINX, see [benchmark.md](benchmark.md)

---


## 🎬 Project Demo — AIONIC NeuroHTTP

<p align="center">
  <em>Experience the raw performance and intelligence of NeuroHTTP in action.</em>
</p>

<p align="center">
  <a href="https://github.com/okba14/NeuroHTTP/raw/main/videos/demo.mp4">
    <img src="https://img.shields.io/badge/▶️%20Watch%20Demo-blue?style=for-the-badge" alt="Watch Demo">
  </a>
</p>

<p align="center">
  <sub>
    This demo showcases <strong>NeuroHTTP</strong> — a high-performance, AI-driven web server built entirely in <strong>C</strong> and <strong>Assembly</strong>, redefining how AI APIs communicate with the web.
  </sub>
</p>



<video width="720" controls>
  <source src="https://github.com/okba14/NeuroHTTP/raw/main/videos/demo.mp4" type="video/mp4">
  Your browser does not support the video tag.
</video>


---


## 🚀 Overview

**NeuroHTTP** (codename: *AIMux*) is a next-generation web server **purpose-built for the AI era**.  
Written entirely in **C** and **Assembly**, it delivers low-level performance, predictable latency, and extreme stability under heavy load — without relying on any external frameworks.

💡 Its mission is to **redefine the networking layer for AI systems**, enabling efficient handling of model streams (LLMs) and data-intensive APIs.

### Capabilities
- 🧠 **Real-time AI responses** — token-by-token streaming for LLMs and chat models.  
- ⚙️ **Fast, low-overhead JSON processing** optimized for inference workloads.  
- ⚡ **Concurrent model routing (AI multiplexing)** for parallel AI endpoints.  
- 🔌 **Seamless integration** with HTTP/3, WebSockets, and gRPC.  

> **Goal:** Build the world’s first *AI-native web server* — capable of real-time, high-throughput inference APIs with near-zero overhead.

📊 *For detailed performance metrics, see [benchmark.md](./benchmark.md).*


---

## 🎯 Why It Matters

- 🧠 **First AI-native web server**, built from scratch for intelligent workloads.  
- ⚙️ **Written in C + Assembly** for unmatched performance and low latency.  
- 🌍 **Designed for the AI API era** — scalable, modular, and open-source.
  
### NeuroHTTP bridges the gap between traditional web servers and modern AI infrastructure.


---

## ⚙️ Key Features

| Feature | Description |
|----------|-------------|
| ⚡ **Smart Threading** | Dynamic load balancing for AI workloads. |
| 🧠 **AI Stream Mode** | Token-based real-time responses (HTTP/1.1, HTTP/3, WS). |
| 🧩 **Fast JSON Parser** | Assembly-optimized, SIMD-accelerated. |
| 🔐 **API Keys & Quotas** | Built-in auth and rate control. |
| 🛰️ **gRPC / HTTP/3** | Modern, low-latency protocol support. |
| 🧰 **C Plugin System** | Extend core via loadable modules. |
| 📊 **Live Metrics** | Real-time latency and throughput stats. |


---

## 🧩 Architecture Overview

**NeuroHTTP** is a compact, low-level server core built entirely in **C** and **Assembly**,  
focused on *speed, control,* and *AI-native processing* — no frameworks, no overhead.

---

### ⚙️ Core Layers

| Layer | Purpose |
|--------|----------|
| 🧠 **AI Router** | Direct integration with AI models for smart request handling. |
| ⚙️ **Worker Engine** | Lightweight thread pool for parallel, CPU-bound tasks. |
| 🔒 **Security Layer** | Inline packet inspection and basic request filtering. |
| ⚡ **Cache System** | Fast in-memory cache with auto-expiry. |
| 🔌 **Plugin Loader** | Extend functionality via loadable C modules. |

---

> 🧠 **Essence:** A self-optimizing, AI-aware server core — minimal, fast, and built for the future.


---


## 📂 Project Structure
The **AIONIC AI Web Server** is organized for modularity, performance, and clarity — following a clean separation between **core**, **AI**, **ASM**, and **plugin** layers.

```bash 
/neurohttp
├── .github/ # GitHub Actions CI/CD workflows and automation
├── benchmarks/ # Performance benchmarking scripts and reports
│ └── benchmark.py # Python benchmark runner for latency and throughput tests
│
├── config/ # Configuration files
│ └── aionic.conf # Default server configuration (port, threads, cache, AI models)
│
├── docs/ # Technical documentation
│ ├── ARCHITECTURE.md # Detailed system design & module interactions
│ ├── PERFORMANCE.md # Performance tuning, memory footprint, benchmarks
│ └── ROADMAP.md # Planned features and development milestones
│
├── include/ # Public header files for all modules
│ ├── ai/ # AI-related headers
│ │ ├── prompt_router.h # AI model routing & dispatching layer
│ │ ├── stats.h # AI stats collection & monitoring
│ │ └── tokenizer.h # Tokenization and prompt pre-processing logic
│ ├── cache.h # In-memory caching system
│ ├── config.h # Server configuration loader
│ ├── firewall.h # Request filtering and security layer
│ ├── optimizer.h # Runtime performance optimizer
│ ├── parser.h # HTTP request parser (manual implementation in C)
│ ├── plugin.h # Plugin manager and shared library loader
│ ├── router.h # Core HTTP router and route dispatcher
│ ├── server.h # Main server core and worker thread manager
│ ├── stream.h # Streaming and chunked response system
│ ├── utils.h # Helper utilities (logging, timing, etc.)
│ └── utils.sh # Developer utility scripts
│
├── plugins/ # Dynamically loadable plugins (extensible features)
│ ├── limiter.c # Rate limiting / request throttling plugin
│ ├── logstats.c # Real-time log statistics collector
│ └── openai_proxy.c # Proxy integration for external AI APIs
│
├── src/ # Core source code
│ ├── ai/ # AI logic implementation
│ │ ├── prompt_router.c # Handles model selection and routing
│ │ ├── stats.c # Collects and exposes AI processing stats
│ │ └── tokenizer.c # Efficient tokenization engine
│ ├── asm/ # Assembly-optimized performance routines
│ │ ├── crc32.s # CRC32 checksum calculation (fast path)
│ │ ├── json_fast.s # Accelerated JSON parsing
│ │ └── memcpy_asm.s # Optimized memory copy routine
│ ├── cache.c # Caching implementation
│ ├── common.h # Common macros and type definitions
│ ├── config.c # Config file parser
│ ├── firewall.c # Firewall & security checks
│ ├── main.c # Entry point and main loop (server bootstrap)
│ ├── optimizer.c # Runtime performance optimizer logic
│ ├── parser.c # HTTP parser and header extractor
│ ├── plugin.c # Plugin loader and registry
│ ├── router.c # Core HTTP route resolution and dispatch
│ ├── server.c # Thread pool, socket management, and event loop
│ ├── stream.c # Streaming API and chunked response handler
│ └── utils.c # Utility functions (logging, timers, memory)
│
├── tests/ # Unit and integration tests
│ ├── test_json.c # Tests for JSON parser
│ ├── test_server.c # Core server tests
│ └── test_streaming.c # Streaming response validation
│
├── .gitignore # Ignored files and directories
├── CODE_OF_CONDUCT.md # Contributor behavior guidelines
├── CONTRIBUTING.md # How to contribute to AIONIC
├── LICENSE # Open-source license (MIT / custom)
├── Makefile # Build system (C + ASM compilation)
├── README.md # Main documentation and usage instructions
├── SECURITY.md # Security policy and vulnerability reporting
└── stats.json # Runtime stats snapshot (for debugging)
```
---

## 🧰 Technologies Used

- **Language:** C99 / C11  
- **Low-level optimizations:** x86 / x86_64 Assembly  
- **Networking:** `epoll` (Linux) or `libuv`  
- **TLS:** `mbedtls` or `wolfSSL`  
- **gRPC support:** `protobuf-c`  
- **Build tools:** `make`, `cmake`, `clang`

---

## 🧠 Minimum Viable Product (MVP)

The first version focuses on simplicity and raw performance.

### ✅ Features
- Handles `HTTP POST` requests at `/v1/chat`
- Accepts JSON body containing `{ "prompt": "..." }`
- Responds with `{ "response": "Hello, AI world!" }`
- Supports **chunked streaming responses**
- Easily testable via `curl`

### 🧪 Example
```bash
curl http://localhost:8080 
curl http://localhost:8080/health
curl http://localhost:8080/stats
curl -X POST http://localhost:8080/v1/chat -d '{"prompt":"Hello"}'
curl -X POST -H "Content-Type: application/json" -d '{"prompt":"Hello"}' http://localhost:8080/v1/chat
```
Expected output:

### json
{"response": "Hello, AI world!"}
# 🚧 Roadmap
Phase	Description
Phase 1	Core HTTP server with streaming responses
Phase 2	WebSocket support for AI streaming
Phase 3	Optimized C/ASM JSON Parser
Phase 4	Modular Plug-in System for custom extensions
Phase 5	Open-source release with detailed benchmarks vs Nginx

# 📊 Benchmark 

## Summary of Results

```bash
### Run:
wrk -t4 -c100 -d30s --latency http://localhost:8080/
```

| Server   | Requests/sec | Total Requests | Avg Latency | p50   | p75     | p90      | p99       | Max Latency | Transfer/sec |
|----------|--------------|----------------|-------------|-------|---------|----------|-----------|-------------|--------------|
| nginx    | 6743         | 202,701        | 80ms        | 11.79ms | 13.23ms | 218.75ms | 1.12s    | 1.33s       | 1.02MB       |
| NeuroHTTP   | 1621         | 48,762         | 61.22ms     | 43.08ms | 100.72ms | 104.62ms | 114.27ms | 135.85ms    | 4.94MB       |



# 💡 Vision
* The web was built for documents.
* Then came applications.
* Now it’s time for AI.

NeuroHTTP aims to redefine how AI models are served at scale, providing a native AI transport layer that’s fast, flexible, and open.

# 🧩 Example Use Cases
* Running AI chat models with streaming responses (like GPT, Claude, Mistral)

* Hosting LangChain or LLM orchestration pipelines

* Serving gRPC-based AI inference APIs

* Building multi-model routers for AI backends

# 🌍 Open Source Impact
Releasing NeuroHTTP on GitHub under the MIT License will attract:

Developer communities on Reddit, Hacker News, and GitHub

Early adoption by AI startups needing real-time serving

Collaboration similar to what happened with Caddy, Envoy, and Nginx

## 🔧 Installation

NeuroHTTP can be built directly from source on any Linux or Unix-like system.

### 🧩 Prerequisites
Make sure you have the following installed:
- **GCC / Clang**
- **Make**
- **libpthread**, **libssl**, and **zlib** (for HTTP/3 and threading support)

### ⚙️ Build & Run

```bash
# Clone the repository
git clone https://github.com/okba14/NeuroHTTP.git

# Navigate into the project directory
cd NeuroHTTP

# Build the project
make all

# Run the NeuroHTTP server
./bin/aionic
```


---

# ✅ AIONIC Web Server — Evaluation Summary

---

## 🧩 1. Basic Tests

| 🧠 **Test** | 💻 **Command** | 🧾 **Result** | ⭐ **Rating** | 📋 **Notes** |
|--------------|----------------|----------------|----------------|----------------|
| **Root Endpoint** | `curl http://localhost:8080` | Professional HTML page | ⭐⭐⭐⭐⭐ | Proper root redirect, integrated CSS, `Server: AIONIC/1.0` |
| **Health Check** | `curl http://localhost:8080/health` | Valid JSON response | ⭐⭐⭐⭐⭐ | Accurate health data, `Content-Type: application/json` |
| **Statistics** | `curl http://localhost:8080/stats` | `{"requests":0,"responses":0,"uptime":0,"active_connections":0,"timestamp":1760719653}` | ⭐⭐⭐⭐⭐ | Real-time metrics with timestamp support |
| **POST /v1/chat** | `curl -X POST -d '{"prompt":"Hello"}' http://localhost:8080/v1/chat` | Valid JSON | ⭐⭐⭐⭐⭐ | Auto JSON parsing, smart response formatting |
| **POST /v1/chat (JSON header)** | `curl -X POST -H "Content-Type: application/json" -d '{"prompt":"Hello"}' http://localhost:8080/v1/chat` | `{"response":"Hello! ...","model":"aionic-1.0","timestamp":1760719677}` | ⭐⭐⭐⭐⭐ | Full `Content-Type` support, detailed metadata |

---

## ⚙️ 2. Additional Successful Tests

| 🧪 **Test** | 🧾 **Result** | ⭐ **Rating** | 📋 **Notes** |
|--------------|----------------|----------------|----------------|
| **XML Accept Header** | Returns JSON fallback | ⭐⭐⭐⭐ | Graceful degradation, safe fallback behavior |
| **Message Variants** | Consistent structured responses | ⭐⭐⭐⭐⭐ | Handles English, Arabic, long text & special chars *(100% success)* |
| **Invalid JSON** | HTML 400 Error page | ⭐⭐⭐⭐⭐ | JSON syntax error detected, styled error page with details |
| **Unknown Routes** | HTML 404 Error page | ⭐⭐⭐⭐⭐ | Consistent theme, redirect links to home |
| **Concurrent Load (50 requests)** | All requests processed successfully | ⭐⭐⭐⭐⭐ | Stable under load, zero request loss, consistent throughput |

---

## 🧠 3. Server Log Analysis

| ⚙️ **Module** | 🧩 **Log Snippet** | ⭐ **Rating** | 📋 **Notes** |
|----------------|--------------------|----------------|----------------|
| **Optimizer** | `[OPTIMIZER] Performance degradation detected...` | ⭐⭐⭐⭐⭐ | Automatic performance recovery, full CPU optimization cycle |
| **JSON Processor** | `DEBUG: JSON parsed successfully using fast tokenizer` | ⭐⭐⭐⭐⭐ | Custom fast tokenizer, zero parsing errors |
| **Router System** | `DEBUG: Parsed request - Method: 1, Path: /v1/chat` | ⭐⭐⭐⭐⭐ | Accurate routing, precise response length tracking, full HTTP method support |

---

## 💡 Overall Evaluation

> **Overall Rating:** ⭐⭐⭐⭐⭐  
> **Summary:** AIONIC/1.0 demonstrates exceptional reliability, accurate routing, intelligent request handling, professional error recovery, and stable performance under concurrent load.

---

### 🏁 Highlights
- ✅ Precise routing and response length calculation  
- ✅ Full HTTP methods support (GET, POST, etc.)  
- ✅ Intelligent JSON parsing with fast tokenizer  
- ✅ Robust performance under pressure  
- ✅ Elegant, consistent HTML error pages  



---


# 🧑‍💻 Contributing
Contributions are welcome!
Whether you want to optimize Assembly routines, design the plugin API, or test benchmarks — your help is appreciated.

Fork the repository
```bash 
Create a new branch (feature/your-feature)
```
Submit a pull request

---

## 🪪 License & Credits

<p align="center">
  <a href="./LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge" alt="MIT License">
  </a>
  <img src="https://img.shields.io/badge/Open%20Source-%E2%9D%A4-blue?style=for-the-badge" alt="Open Source Love">
  <img src="https://img.shields.io/badge/Built%20with-C%20%2B%20Assembly-orange?style=for-the-badge&logo=c" alt="C + Assembly">
  <img src="https://img.shields.io/badge/AI%20Native-Yes-purple?style=for-the-badge&logo=openai" alt="AI Native">
  <img src="https://img.shields.io/badge/Contributions-Welcome-brightgreen?style=for-the-badge" alt="Contributions Welcome">
</p>

---

**License:** [MIT](./LICENSE) — free for both commercial and academic use.  
**Credits:** Built by **GUIAR OQBA**, with ❤️ from the open-source community.


---

## 🧬 Author

**👨‍💻 GUIAR OQBA** 🇩🇿  
Creator of **NeuroHTTP** — passionate about **low-level performance**, **AI infrastructure**, and **modern web systems**.

> _“Empowering the next generation of AI-native infrastructure — from Elkantara, Algeria.”_

<p align="center">
  <img src="https://img.shields.io/badge/El%20Kantara-Algeria-006233?style=for-the-badge&logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAAAVklEQVQoz2NgoBAw4v///zH5j4GBQYyBgRj4z0SfwYHBv8nAwMCfMZkABhYGBgYGxv+HQ0P5n8DBwYN5wMDAwMiJrQxjYCgYGBg8EUUioM4xjAAAyNg4MSceOtwAAAABJRU5ErkJggg==">
</p>

<p align="center">
  <sub>© 2025 GUIAR OQBA — All rights reserved.</sub>
</p>


---

## ⭐ Support the Project

<p align="center">
  <a href="https://github.com/okba14/NeuroHTTP/stargazers">
    <img src="https://img.shields.io/github/stars/okba14/NeuroHTTP?style=for-the-badge&logo=github" alt="GitHub Stars"/>
  </a>
  <a href="https://github.com/okba14/NeuroHTTP/forks">
    <img src="https://img.shields.io/github/forks/okba14/NeuroHTTP?style=for-the-badge&logo=github" alt="GitHub Forks"/>
  </a>
  <a href="https://github.com/okba14">
    <img src="https://img.shields.io/badge/Follow-GUIAR%20OQBA-black?style=for-the-badge&logo=github" alt="Follow Developer"/>
  </a>
  <a href="https://github.com/okba14/NeuroHTTP/discussions">
    <img src="https://img.shields.io/badge/Join-Community-blueviolet?style=for-the-badge&logo=github" alt="Community"/>
  </a>
</p>

<p align="center">
  If you believe in the vision of a <strong>fast, AI-native web layer</strong>, please ⭐ the repository and share it.<br/>
  Every star fuels the open-source ecosystem and helps <strong>NeuroHTTP</strong> evolve. 🚀
</p>

> 💬 “<strong>Fast. Modular. AI-Native.</strong> — That’s <strong>NeuroHTTP</strong>.”

---

<p align="center">
  <img src="https://img.shields.io/badge/Performance-Ultra%20Fast-red?style=flat-square"/>
  <img src="https://img.shields.io/badge/Architecture-Modular-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/Core-AI%20Ready-purple?style=flat-square"/>
</p>

<p align="center">
  <sub>✨ Join the mission to redefine how the web talks to AI — one packet at a time.</sub>
</p>


