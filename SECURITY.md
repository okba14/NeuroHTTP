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
- Networking stack and protocol handlers (HTTP/3, WebSockets, gRPC)
- Plugin and module interface (`plugins/`)
- Authentication, API key, and token mechanisms

It does **not** cover:
- Third-party libraries used (e.g., mbedTLS, protobuf-c)
- User-created plugins or modifications

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
