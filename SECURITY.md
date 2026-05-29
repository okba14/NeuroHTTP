# 🔒 Security Policy

## 🧠 Overview
**NeuroHTTP** is an open-source, high-performance AI-native web server built in **C** and **Assembly**.  
Given its focus on low-level performance and networking, **security is a top priority**.  
This document explains how to responsibly report vulnerabilities and how the team handles them.

---

## 📬 Reporting a Vulnerability

If you discover a **security vulnerability**, please **do not open a public GitHub issue**.  
Instead, contact the maintainer directly via:

- **Email:** [techokba@gmail.com](mailto:techokba@gmail.com)
- **GitHub Security Advisories:** [Submit privately here](https://github.com/okba14/NeuroHTTP/security/advisories/new)

Please include as much detail as possible:
- Description of the vulnerability  
- Steps to reproduce  
- Potential impact or exploitation scenarios  
- Suggested mitigations (if any)

---

## 🕒 Response Timeline

Once a report is received:
1. **Acknowledgment:** You’ll receive a confirmation within **48 hours**.
2. **Investigation:** The issue will be validated and analyzed (1–5 business days).
3. **Fix & Release:** A patch or mitigation will be prepared and tested.
4. **Disclosure:** The vulnerability will be responsibly disclosed after a safe update is available.

---

## 🧩 Scope

This policy covers:
- Core server code (`src/`)
- Thread and memory management routines
- Networking stack and protocol handlers (HTTP/1.1, HTTP/2, TLS)
- Plugin and module interface (`plugins/`)
- Authentication, API key, and token mechanisms

It does **not** cover:
- Third-party libraries used (e.g., OpenSSL, libnghttp2, libcurl)
- User-created plugins or modifications

> **Note:** HTTP/3, WebSocket, and gRPC support are planned but not yet implemented.
> Claims in other documents about their current availability are aspirational.

---

## 🔍 Known Security Audit Findings (May 2026)

The following issues were identified during a comprehensive code audit. They should be addressed before production deployment:

### CRITICAL
- **Stack buffer overflow in base64 decoder** (`src/server.c:57-65`): `b64_decode()` writes decoded output without checking against the provided output buffer size. An oversized HTTP2-Settings header can overflow a 128-byte stack buffer. **Fixed in latest commit**.
- **Placeholder API key in config** (`config/aionic.conf`): The tracked config file previously contained `api_key = your-secret-api-key-here`. API keys should only be supplied via environment variables. **Fixed in latest commit**.

### HIGH
- **Content-Length integer overflow** (`src/server.c:284,894`): Parsing `Content-Length` with `atoi()` allows negative values and silent wraparound, potentially bypassing body size checks. **Fixed in latest commit** (uses `strtol` with validation).
- **Uninitialized firewall mutex** (`src/firewall.c:50`): `global_firewall.mutex` was used without static or dynamic initialization. **Fixed in latest commit**.

### MEDIUM
- **Unchecked strdup() returns in config parser** (`src/config.c:43,90-93`): OOM conditions could lead to NULL pointer dereference. **Fixed in latest commit**.
- **Empty plugin file** (`plugins/limiter.c`): 0-byte file would crash `dlopen`. **Fixed in latest commit**.

### Notes
- `config/key.pem` and `config/cert.pem` exist on disk but are gitignored (`config/*.pem` in `.gitignore`). They should be treated as secrets and regenerated for production.
- The AI model definitions in `aionic.conf` correctly reference environment variable names (e.g., `GROQ_API_KEY`) rather than embedding keys.

---

## 🤝 Responsible Disclosure

We strongly encourage responsible, coordinated disclosure.
Security researchers who follow this policy will receive full credit in release notes and acknowledgments.

If you’ve found something critical, your contribution may also be featured in the **"Security Hall of Fame"** section of the README.

---

## 🧑‍💻 Maintainer

**GUIAR OQBA** 🇩🇿  
Creator & Lead Developer of **NeuroHTTP**  
Focused on AI infrastructure, performance, and security in low-level systems.

📧 [techokba@gmail.com](mailto:techokba@gmail.com)  
🌐 [https://github.com/okba14/NeuroHTTP](https://github.com/okba14/NeuroHTTP)

---

> “Performance without security is just an exploit waiting to happen.”  
> — _NeuroHTTP Security Team_
