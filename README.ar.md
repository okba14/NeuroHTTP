<h1 align="center">⚡ NeuroHTTP — خادم ويب للذكاء الاصطناعي (C + ASM)</h1>

<p align="center">
  <img src="https://img.shields.io/badge/language-C%20%2B%20ASM-blue?style=flat-square" />
  <img src="https://img.shields.io/badge/build-passing-brightgreen?style=flat-square" />
  <img src="https://img.shields.io/badge/SIMD-SSE4.2%2FAVX2%2FAVX512-orange?style=flat-square" />
  <img src="https://img.shields.io/badge/TLS%201.3-OpenSSL%203.6-blue?style=flat-square" />
  <img src="https://img.shields.io/badge/HTTP%2F2-nghttp2%201.69-blue?style=flat-square" />
  <img src="https://img.shields.io/badge/io_uring-Linux%206.19-purple?style=flat-square" />
  <img src="https://img.shields.io/badge/license-MIT-green?style=flat-square" />
</p>

<p align="center">
  <em>خادم ويب عالي الأداء للذكاء الاصطناعي، مبني من الصفر بلغة C مع نواة Assembly مكتوبة يدوياً (SSE2/AVX2/AVX-512).</em><br>
  <em>يدعم 10 مزودي ذكاء اصطناعي — OpenAI، Groq، Anthropic Claude، Google Gemini، DeepSeek، Moonshot/Kimi، Zhipu GLM، Perplexity، Mistral AI، والنماذج المحلية عبر Ollama — كل هذا في ملف تنفيذي واحد.</em>
</p>

<p align="center">
  <a href="README.md"><strong>📖 English Version</strong></a>
</p>

---

## 📋 فهرس المحتويات

- [المميزات](#-المميزات)
- [المزودون المدعومون](#-المزودون-المدعومون)
- [المتطلبات الأساسية](#-المتطلبات-الأساسية)
- [بناء وتشغيل المشروع](#-بناء-وتشغيل-المشروع)
- [الإعدادات](#-الإعدادات)
- [إضافة مفاتيح API](#-إضافة-مفاتيح-api)
- [إضافة نماذج الذكاء الاصطناعي](#-إضافة-نماذج-الذكاء-الاصطناعي)
- [نقاط النهاية API](#-نقاط-النهاية-api)
- [نظام الإضافات (Plugins)](#-نظام-الإضافات-plugins)
- [دليل Makefile](#-دليل-makefile)
- [هندسة المشروع](#-هندسة-المشروع)
- [مقارنة الأداء](#-مقارنة-الأداء)
- [حل المشكلات](#-حل-المشكلات)

---

## 🔥 المميزات

### ⚡ نواة أداء فائقة
- **محرك يعتمد على الأحداث** — حلقة أحداث epoll وحيدة الخيط مع كشف io_uring
- **io_uring غير متزامن** — تكامل كامل مع io_uring لإدخال/إخراج بدون استدعاءات نظام؛ يتراجع إلى epoll بسلاسة؛ دعم SQPOLL (Linux 5.11+)
- **Assembly مكتوب يدوياً** — CRC32 (SSE4.2)، محلل JSON (SSE2/AVX2)، نسخ الذاكرة (SSE2–AVX-512)، تعليمات non-temporal
- **إدخال/إخراج بدون نسخ (Zero-copy)** — دعم `sendfile`/`splice` لخدمة الملفات الثابتة بكفاءة
- **مخصص ذاكرة (Arena allocator)** — تخصيص سريع بدون قفل لكل طلب
- **محلل HTTP بحالة آلية (state-machine)** — بدون sscanf أو regex، محلل HTTP/1.1 نقي

### 🧠 توجيه ذكي لمزودي AI متعددين
- **10 مزودين** — OpenAI، Groq، Anthropic، Gemini، DeepSeek، Moonshot/Kimi، Zhipu GLM، Perplexity، Mistral AI، Ollama
- **توجيه ذكي** — يأخذ زمن الاستجابة والفحص الصحي في الاعتبار، مع احتياطي تلقائي عند الفشل
- **صيغة موحدة** — تُعرّف النماذج في `config/aionic.conf` بصيغة بسيطة

### 🛡️ أمان على مستوى المؤسسات
- **جدار ناري لتطبيقات الويب** — كشف SQLi، XSS، اختراق المسارات؛ قائمة سوداء/بيضاء للـ IP
- **محدد معدل الطلبات (Token bucket)** — تحديد معدل الطلبات لكل IP مع RPS قابل للتكوين
- **فحص الطلبات** — وكيل مستخدم مشبوه، حدود حجم الحمولة

### 📊 المراقبة والإحصائيات
- **مقاييس Prometheus** — نقطة نهاية `/metrics` لجمع المقاييس
- **تتبع الطلبات** — معرفات فريدة لكل طلب، تتبع زمن الاستجابة
- **إحصائيات المزودين** — عدد المكالمات، زمن الاستجابة، استخدام الرموز، معدل الأخطاء لكل مزود
- **إحصائيات السيرفر** — نقطة نهاية `/stats` بمقاييس حية

### 🔌 قابلية التوسع
- **نظام الإضافات (Plugins)** — 6 نقاط ربط (قبل الطلب، بعد الطلب، قبل AI، بعد AI، اتصال، قطع اتصال)
- **عازل طلبات ديناميكي** — لا حدود ثابتة على حجم الطلبات

### 🌐 الشبكات
- **TLS 1.3** — دعم كامل لـ TLS 1.3 عبر OpenSSL 3.6 (أو BoringSSL)؛ OCSP stapling، مفاوضة ALPN، ترويسات HSTS
- **HTTP/2** — h2 عبر TLS (ALPN) + h2c عبر الترقية الصريحة بواسطة nghttp2؛ تدفقات متعددة، دفع الخادم، التحكم في التدفق
- **HTTP/1.1 keep-alive** — إعادة استخدام الاتصال مع مهلة قابلة للتكوين
- **SSE (Server-Sent Events)** — نقطة نهاية البث المباشر في `/v1/chat/stream`
- **TCP defer accept** — تقليل حمل القبول
- **التحقق من SSL قابل للتكوين**

### 🛡️ إيقاف تشغيل آمن (Graceful Shutdown)
- **تصريف الاتصالات** — عند استقبال SIGTERM/SIGINT، يتوقف عن قبول اتصالات جديدة، يُصرف الاتصالات النشطة (بمهلة قابلة للتكوين)، ثم ينظف الموارد
- **معالجة الإشارات** — SIGUSR1 يُحدّث OCSP، SIGHUP يُعيد تحميل مفاتيح API

---

## 🧩 المزودون المدعومون

| المزود | النماذج | الرابط |
|--------|---------|--------|
| **Groq** 🆓 | Llama 3.3 70B، Llama 3.1 8B، Gemma 2 9B، Mixtral 8×7B، DeepSeek R1، Qwen QWQ 32B | `api.groq.com` |
| **OpenAI** 💰 | GPT-4o، GPT-4o-mini، GPT-4 Turbo، o3-mini | `api.openai.com` |
| **Anthropic** 💰 | Claude 3.5 Sonnet، Claude 3.5 Haiku، Claude 3 Opus | `api.anthropic.com` |
| **Google Gemini** 💰 | Gemini 2.0 Flash، Gemini 1.5 Pro | `generativelanguage.googleapis.com` |
| **DeepSeek** 💰 | DeepSeek Chat، DeepSeek Reasoner | `api.deepseek.com` |
| **Moonshot/Kimi** 💰 | Moonshot v1 8K، Moonshot v1 32K | `api.moonshot.cn` |
| **Zhipu (GLM)** 💰 | GLM-4-Plus، GLM-4-Flash | `open.bigmodel.cn` |
| **Perplexity** 💰 | Sonar Pro، Sonar Deep Research | `api.perplexity.ai` |
| **Mistral AI** 💰 | Mistral Large، Mistral Small | `api.mistral.ai` |
| **Ollama محلي** 🆓 | أي نموذج محلي عبر Ollama | `localhost:11434` |

---

## 📦 المتطلبات الأساسية

```bash
# Ubuntu / Debian
sudo apt-get update
sudo apt-get install -y build-essential nasm libcurl4-openssl-dev libssl-dev libnghttp2-dev liburing-dev

# Fedora / RHEL
sudo dnf install -y gcc nasm libcurl-devel openssl-devel libnghttp2-devel liburing-devel

# Arch Linux
sudo pacman -S --noconfirm gcc nasm libcurl-compat openssl nghttp2 liburing

# تأكد من تثبيت NASM (مطلوب لملفات Assembly)
nasm --version   # يجب أن يظهر الإصدار 2.x+
```

---

## 🛠 بناء وتشغيل المشروع

### أوامر البناء

```bash
git clone https://github.com/okba14/NeuroHTTP.git
cd NeuroHTTP

# بناء للإنتاج (محسّن مع LTO، -O3، -march=native)
make

# بناء للتصحيح (AddressSanitizer، UBSan، حماية stack)
make debug

# بناء نظيف من الصفر
make rebuild

# بناء الإضافات فقط
make plugins
```

### أوامر التشغيل

```bash
# تشغيل السيرفر (المنفذ الافتراضي 8080)
./bin/aionic

# تشغيل نسخة التصحيح
./bin/aionic-debug

# أو عبر Make
make run
make run-debug
```

عند التشغيل، سيظهر:
```
========================================
    AIONIC AI Web Server v2.0.0
========================================
Build: May 29 2026
Features: TLS1.3 HTTP/2 io_uring OCSP
========================================
    - Port: 8080
    - TLS Port: 8443
    - Threads: 4
    - Max Connections: 1024
    - io_uring: enabled
    - Zero-Copy: enabled
    - TLS: disabled
    - HTTP/2: enabled
    - Smart Routing: enabled
    - Streaming: enabled
    - Graceful Shutdown Timeout: 30s
========================================

---

## ⚙️ الإعدادات

جميع الإعدادات في ملف `config/aionic.conf`. إليك شرح كل خيار:

### إعدادات السيرفر

```ini
port = 8080                    # منفذ الاستماع HTTP
thread_count = 4               # عدد خيوط العمل (قديم، حلقة الأحداث تستخدم الخيط الرئيسي)
worker_threads = 4             # حجم تجمع خيوط حلقة الأحداث
max_connections = 1024         # الحد الأقصى للاتصالات المتزامنة
request_timeout = 30000        # مهلة الطلب بالميلي ثانية (30 ثانية)
buffer_size = 8192             # حجم عازل الإدخال/الإخراج الداخلي
max_request_size = 33554432    # الحد الأقصى لحجم جسم الطلب (32 ميغابايت)
log_file = logs/aionic.log     # مسار ملف السجل
```

### إعدادات التخزين المؤقت

```ini
enable_cache = 1               # تفعيل التخزين المؤقت للردود
cache_size = 1000              # الحد الأقصى لعدد الإدخالات في ذاكرة التخزين المؤقت
cache_ttl = 3600               # مدة صلاحية ذاكرة التخزين المؤقت بالثواني (ساعة واحدة)
```

### إعدادات الأمان

```ini
enable_firewall = 1            # تفعيل جدار الحماية (كشف SQLi، XSS، اختراق المسارات)
verify_ssl = 1                 # التحقق من شهادات SSL لمزودي AI
api_key = your-secret-api-key-here  # مفتاح API عام للسيرفر للتحقق من هوية العملاء
```

### إعدادات TLS / HTTPS (v2.0.0)

```ini
enable_tls = 0                      # تفعيل TLS 1.3 HTTPS (يتطلب شهادة ومفتاح)
tls_port = 8443                     # منفذ استماع HTTPS
tls_cert_file = config/cert.pem     # مسار شهادة TLS (PEM)
tls_key_file = config/key.pem       # مسار مفتاح TLS الخاص (PEM)
tls_ca_file = config/ca.pem         # حزمة CA لـ OCSP (اختياري)
tls_enable_ocsp = 0                 # تفعيل OCSP stapling
tls_ocsp_refresh_interval = 3600    # فترة تحديث OCSP بالثواني
tls_hsts_max_age = 31536000         # أقصى عمر HSTS بالثواني (سنة واحدة)
```

### إعدادات HTTP/2 (v2.0.0)

```ini
enable_http2 = 1                    # تفعيل HTTP/2 (h2 عبر ALPN + ترقية h2c)
http2_max_concurrent_streams = 256  # أقصى تدفقات متزامنة لكل اتصال
http2_max_header_list_size = 65536  # أقصى حجم لقائمة الترويسات
http2_initial_window_size = 65535   # حجم نافذة التحكم في التدفق الأولي
http2_max_frame_size = 16384        # أقصى حجم للإطار
```

### إعدادات المحرك

```ini
enable_iouring = 1             # تفعيل io_uring غير المتزامن (يتراجع إلى epoll)
enable_zero_copy = 1           # تفعيل الإدخال/الإخراج بدون نسخ (sendfile/splice)
enable_ratelimiter = 1         # تفعيل تحديد معدل الطلبات لكل IP
rate_limit_rps = 100           # الحد الأقصى للطلبات في الثانية لكل IP
enable_observability = 1       # تفعيل المقاييس والتتبع
enable_streaming = 1           # تفعيل نقطة نهاية البث المباشر SSE
enable_smart_routing = 1       # تفعيل التوجيه الذكي بناءً على زمن الاستجابة والفحص الصحي
enable_keepalive = 1           # تفعيل HTTP/1.1 keep-alive
keepalive_timeout = 30         # مهلة keep-alive بالثواني
```

---

## 🔑 إضافة مفاتيح API

تتم قراءة مفاتيح API من **متغيرات البيئة** للأمان. لا تضع المفاتيح مباشرة في ملف الإعدادات أبداً.

### تعيين مفاتيح API

```bash
# Groq (مجاني، احصل على المفتاح من https://console.groq.com/keys)
export GROQ_API_KEY="gsk_مفتاحك_هنا"

# OpenAI (احصل على المفتاح من https://platform.openai.com/api-keys)
export OPENAI_API_KEY="sk_مفتاحك_هنا"

# Anthropic Claude (احصل على المفتاح من https://console.anthropic.com/)
export ANTHROPIC_API_KEY="sk-ant-مفتاحك"

# Google Gemini (احصل على المفتاح من https://aistudio.google.com/apikey)
export GEMINI_API_KEY="AIza_مفتاحك"

# DeepSeek (احصل على المفتاح من https://platform.deepseek.com/)
export DEEPSEEK_API_KEY="sk_مفتاحك"

# Moonshot / Kimi
export MOONSHOT_API_KEY="مفتاحك"

# Zhipu GLM
export ZHIPU_API_KEY="مفتاحك"

# Perplexity
export PERPLEXITY_API_KEY="pplx_مفتاحك"

# Mistral AI
export MISTRAL_API_KEY="مفتاحك"
```

### حفظ المفاتيح بشكل دائم

أضفها إلى ملف شيل الخاص بك (`~/.bashrc`، `~/.zshrc`، إلخ):

```bash
echo 'export GROQ_API_KEY="gsk_مفتاحك"' >> ~/.bashrc
echo 'export OPENAI_API_KEY="sk_مفتاحك"' >> ~/.bashrc
source ~/.bashrc
```

أو استخدم ملف `.env`:

```bash
# .env
GROQ_API_KEY=gsk_xxxx
OPENAI_API_KEY=sk-xxxx

# حمّل قبل التشغيل
export $(grep -v '^#' .env | xargs) && ./bin/aionic
```

### ملاحظة مهمة
- اسم متغير البيئة في ملف الإعدادات (`config/aionic.conf`) يجب أن يطابق اسم المتغير الذي قمت بتعيينه
- مثال: إذا كان الإعداد يقول `GROQ_API_KEY`، فيجب أن يكون `export GROQ_API_KEY="..."`

---

## 🧠 إضافة نماذج الذكاء الاصطناعي

تُعرّف النماذج في `config/aionic.conf` بهذه الصيغة:

```
ai_model = <الاسم>|<الرابط>|<متغير_البيئة>|<الحد_الأقصى_للرموز>|<درجة_الحرارة>
```

| الحقل | الوصف | مثال |
|-------|-------|------|
| `الاسم` | معرف النموذج (يُستخدم في استدعاءات API) | `gpt-4o` |
| `الرابط` | رابط API الكامل | `https://api.openai.com/v1/chat/completions` |
| `متغير_البيئة` | اسم متغير البيئة الذي يحتوي على مفتاح API | `OPENAI_API_KEY` |
| `الحد_الأقصى_للرموز` | أقصى عدد رموز للاستجابة | `16384` |
| `درجة_الحرارة` | درجة حرارة أخذ العينات (0.0–1.0) | `0.7` |

### أمثلة

```ini
# متوافق مع OpenAI (OpenAI، Groq، Together، vLLM، LocalAI، Ollama)
ai_model = gpt-4o|https://api.openai.com/v1/chat/completions|OPENAI_API_KEY|16384|0.7
ai_model = llama-3.3-70b-versatile|https://api.groq.com/openai/v1/chat/completions|GROQ_API_KEY|8192|0.7

# Anthropic Claude (يستخدم نقطة النهاية /v1/messages)
ai_model = claude-3-5-sonnet-20241022|https://api.anthropic.com/v1/messages|ANTHROPIC_API_KEY|8192|0.7

# Google Gemini (يستخدم نقطة النهاية generateContent)
ai_model = gemini-2.0-flash|https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent|GEMINI_API_KEY|8192|0.7

# نموذج محلي عبر Ollama (يجب أن يكون Ollama قيد التشغيل على المنفذ 11434)
ai_model = llama3-local|http://localhost:11434/v1/chat/completions|OPENAI_API_KEY|4096|0.7
```

### إضافة مزود مخصص

1. أضف سطر النموذج إلى `config/aionic.conf`
2. صدّر مفتاح API المقابل كمتغير بيئة
3. أعد تشغيل السيرفر

سيظهر النموذج تلقائياً في `/v1/models` وسيكون متاحاً لمحادثات AI.

### إضافة Groq (مجاني تماماً)

Groq يوفر نماذج Llama 3 و Mixtral و Gemma مجاناً. كل ما تحتاجه:

```bash
# 1. سجل في https://console.groq.com/keys
# 2. احصل على مفتاح API مجاني
export GROQ_API_KEY="gsk_مفتاحك"

# 3. النماذج موجودة مسبقاً في الإعدادات
# 4. شغّل السيرفر
./bin/aionic
```

---

## 🌐 نقاط النهاية API

### `GET /health` — فحص الصحة

التحقق من أن السيرفر يعمل بشكل طبيعي.

```bash
curl http://localhost:8080/health
```

```json
{
  "status": "ok",
  "timestamp": 1779698657,
  "server": "AIONIC/1.0",
  "version": "1.0.0"
}
```

### `GET /v1/models` — عرض النماذج

عرض جميع نماذج AI المُعدّة.

```bash
curl http://localhost:8080/v1/models
```

```json
{
  "models": ["llama-3.3-70b-versatile", "gpt-4o", "claude-3-5-sonnet", ...],
  "count": 27
}
```

### `POST /v1/chat` — محادثة مع AI

إرسال طلب إلى نموذج AI والحصول على الرد.

```bash
curl -X POST http://localhost:8080/v1/chat \
  -H "Content-Type: application/json" \
  -d '{"prompt": "ما هي عاصمة فرنسا؟", "model": "llama-3.3-70b-versatile"}'
```

**المعاملات:**
- `prompt` (مطلوب) — النص المرسل إلى النموذج
- `model` (اختياري) — اسم النموذج (يستخدم النموذج الافتراضي إذا حُذف)

```json
{
  "response": "عاصمة فرنسا هي باريس.",
  "model": "llama-3.3-70b-versatile",
  "usage": {
    "prompt_tokens": 12,
    "completion_tokens": 8
  }
}
```

### `POST /v1/chat/stream` — محادثة بث مباشر

نفس `/v1/chat` ولكن يُرسل الرد كأحداث SSE (Server-Sent Events).

```bash
curl -N -X POST http://localhost:8080/v1/chat/stream \
  -H "Content-Type: application/json" \
  -d '{"prompt": "احك لي قصة", "model": "llama-3.3-70b-versatile"}'
```

صيغة الإخراج:
```
id: <معرف_الطلب>
event: message
data: <جزء>

data: [DONE]
```

### `GET /stats` — إحصائيات السيرفر

عرض مقاييس حية للسيرفر.

```bash
curl http://localhost:8080/stats
```

```json
{
  "requests": 42,
  "responses": 42,
  "active_connections": 1,
  "total_errors": 0,
  "total_ai_calls": 10,
  "uptime_seconds": 3600,
  "bytes_sent": 1048576,
  "bytes_received": 65536,
  "timestamp": 1779698657
}
```

### `GET /metrics` — مقاييس Prometheus

عرض المقاييس بصيغة Prometheus النصية لجمعها.

```bash
curl http://localhost:8080/metrics
```

```
# HELP aionic_requests_total مجموع طلبات HTTP
# TYPE aionic_requests_total counter
aionic_requests_total 42
# HELP aionic_ai_calls_total مجموع استدعاءات نماذج AI
# TYPE aionic_ai_calls_total counter
aionic_ai_calls_total 10
```

### `GET /v1/providers` — قائمة المزودين

عرض قائمة مزودي AI المُعدّين.

```bash
curl http://localhost:8080/v1/providers
```

### `GET /` — الصفحة الرئيسية

عرض صفحة الترحيب أو الشعار.

```bash
curl http://localhost:8080/
```

---

## 🔌 نظام الإضافات (Plugins)

يدعم NeuroHTTP التحميل الديناميكي لمكتبات الإضافات (`.so`) في وقت التشغيل.

### هيكل الإضافة

كل إضافة عبارة عن ملف `.so` في مجلد `plugins/` بهذه الدوال:

| الدالة | نقطة الربط | متى تُستدعى |
|--------|-----------|-------------|
| `plugin_init()` | — | عند بدء تشغيل السيرفر |
| `plugin_hook(0, ctx)` | `PLUGIN_HOOK_PRE_REQUEST` | قبل توجيه الطلب |
| `plugin_hook(1, ctx)` | `PLUGIN_HOOK_POST_REQUEST` | بعد التوجيه، قبل الرد |
| `plugin_hook(2, ctx)` | `PLUGIN_HOOK_AI_PROMPT` | قبل استدعاء AI |
| `plugin_hook(3, ctx)` | `PLUGIN_HOOK_AI_RESPONSE` | بعد رد AI |
| `plugin_hook(4, ctx)` | `PLUGIN_HOOK_ON_CONNECT` | اتصال عميل جديد |
| `plugin_hook(5, ctx)` | `PLUGIN_HOOK_ON_DISCONNECT` | قطع اتصال العميل |
| `plugin_cleanup()` | — | عند إيقاف تشغيل السيرفر |

### بناء الإضافات

```bash
# بناء جميع الإضافات
make plugins

# ملفات .so تُخرج إلى build/plugins/
```

### الإضافات المضمنة

| الإضافة | الملف | الغرض |
|--------|-------|-------|
| `logstats` | `plugins/logstats.c` | تسجيل جميع الطلبات واستدعاءات AI |
| `openai_proxy` | `plugins/openai_proxy.c` | اعتراض استدعاءات AI لإضافة تعليمات النظام |

### إنشاء إضافة مخصصة

```c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>

int plugin_init(void) {
    printf("[إضافتي] تم التهيئة\n");
    return 0;
}

int plugin_hook(int hook_point, void *ctx) {
    if (hook_point == 0) {  /* PRE_REQUEST */
        printf("[إضافتي] تم استلام طلب\n");
    }
    return 0;
}

void plugin_cleanup(void) {
    printf("[إضافتي] تم التنظيف\n");
}
```

الترجمة: `gcc -fPIC -shared myplugin.c -o build/plugins/myplugin.so`

---

## 🛠 دليل Makefile

| الأمر | الوصف |
|-------|-------|
| `make` | بناء النسخة الإنتاجية |
| `make debug` | بناء مع AddressSanitizer + UBSan |
| `make rebuild` | تنظيف + بناء كامل |
| `make run` | بناء وتشغيل السيرفر |
| `make run-debug` | بناء وتشغيل نسخة التصحيح |
| `make plugins` | بناء ملفات الإضافات `.so` |
| `make install` | تثبيت إلى `/usr/local/bin/aionic` |
| `make uninstall` | إزالة الملفات المثبتة |
| `make clean` | حذف ملفات البناء |
| `make test` | بناء وتشغيل الاختبارات |
| `make benchmark` | عرض أمر الاختبار |
| `make docs` | إنشاء وثائق Doxygen |
| `make analyze` | تشغيل تحليل cppcheck الثابت |
| `make format` | تنسيق الكود بـ clang-format |
| `make memcheck` | تشغيل فحص الذاكرة بـ valgrind |
| `make profile` | تحليل الأداء بـ gprof |

---

## 🏗 هندسة المشروع

```
NeuroHTTP/
├── src/                    # ملفات مصدر C
│   ├── main.c              # نقطة الدخول، معالجة الإشارات، حلقة التهيئة
│   ├── server.c            # خادم HTTP، إدارة الاتصالات، تكامل حلقة الأحداث
│   ├── iouring_engine.c    # محرك io_uring غير متزامن مع احتياطي epoll
│   ├── tls.c               # غلاف TLS 1.3 (OpenSSL 3.6 / BoringSSL)، OCSP stapling، ALPN
│   ├── http2.c             # إدارة جلسات HTTP/2 عبر nghttp2 (h2 + h2c)
│   ├── router.c            # موزع المسارات بجدول تجزئة مع middleware
│   ├── http_parser.c       # محلل HTTP/1.1 بحالة آلية
│   ├── config.c            # محلل ملف الإعدادات
│   ├── arena.c             # مخصص الذاكرة Arena + Slab + ArenaPool
│   ├── ratelimiter.c       # محدد معدل الطلبات Token-bucket + sliding-window
│   ├── observability.c     # مقاييس، تتبع، إحصائيات المزودين
│   ├── firewall.c          # جدار ناري مع كشف SQLi/XSS، قائمة سوداء IP
│   ├── parser.c            # محلل HTTP قديم
│   ├── stream.c            # دعم البث المباشر SSE
│   ├── cache.c             # تخزين مؤقت للردود
│   ├── plugin.c            # محمل إضافات ديناميكي
│   ├── optimizer.c         # محسّن وقت التشغيل
│   ├── utils.c             # دوال مساعدة
│   ├── ai/
│   │   ├── prompt_router.c # توجيه AI متعدد المزودين (ذكي بالزمن/الصحة)
│   │   ├── stats.c         # إحصائيات لكل نموذج
│   │   └── tokenizer.c     # عد الرموز
│   └── asm/
│       ├── crc32.s         # CRC32 (SSE4.2) — التجزئة
│       ├── json_fast.s     # محلل JSON (SSE2/AVX2)
│       └── memcpy_asm.s    # نسخ الذاكرة (SSE2–AVX-512)
├── include/                # ملفات الرأس
│   ├── tls.h               # واجهة TLS 1.3 + OCSP + ALPN
│   ├── http2.h             # واجهة جلسات HTTP/2
│   └── ...
├── config/
│   └── aionic.conf         # ملف الإعدادات الرئيسي
├── plugins/                # ملفات مصدر الإضافات
└── Makefile
```

### تدفق البيانات

```
عميل → TCP Accept (عادي / TLS)
         │
         ├── مصافحة TLS 1.3 (إذا كان منفذ TLS)
         │   ├── OCSP stapling، مفاوضة ALPN
         │   └── ترويسات HSTS
         │
         ↓
   حلقة الأحداث (epoll / io_uring)
         │
         ├── HTTP/2؟ ──► ترقية h2c أو ALPN h2
         │                   └── جلسة nghttp2 → تدفقات متعددة
         │
         ↓
    محلل HTTP (حالة آلية HTTP/1.1 أو إطارات HTTP/2)
         ↓
      جدار النار (WAF)
         ↓
    محدد معدل الطلبات
         ↓
      موزع المسارات
         ├── /health → معالج الصحة
         ├── /v1/models → معالج النماذج
         ├── /v1/chat → موجه مزود AI
         │                ├── Groq
         │                ├── OpenAI
         │                ├── Anthropic
         │                ├── Gemini
         │                └── ...
         ├── /metrics → معالج Prometheus
         ├── /stats → معالج الإحصائيات
         └── /v1/providers → معالج المزودين
         ↓
   رد → send() / sendfile() / nghttp2 submit_response()
         ↓
   إيقاف تشغيل آمن (SIGTERM/SIGINT)
      ├── التوقف عن قبول اتصالات جديدة
      ├── تصريف الاتصالات النشطة (مدة قابلة للتكوين)
      └── تنظيف الموارد → خروج
```

---

## 📈 مقارنة الأداء

| السيرفر | الاتصالات | طلب/ثانية | زمن الاستجابة | نقل/ثانية |
|---------|-----------|-----------|--------------|-----------|
| NGINX 1.29.3 | 10k | 8,148 | 114ms | 1.2 MB/s |
| **NeuroHTTP** | **10k** | **2,593** | **57ms** | **7.9 MB/s** |

> NeuroHTTP يتعامل مع حمولات AI أثقل بزمن استجابة أقل.

---

## 🔧 حل المشكلات

### السيرفر لا يعمل — "Address already in use"

```bash
# ابحث عن العملية التي تستخدم المنفذ 8080
fuser 8080/tcp

# اقتلها
fuser -k 8080/tcp

# أو استخدم منفذاً مختلفاً
# عدّل config/aionic.conf: port = 8081
```

### لا توجد نماذج متاحة

```bash
# تأكد من تعيين مفاتيح API
echo $GROQ_API_KEY
echo $OPENAI_API_KEY

# تأكد من صيغة ملف الإعدادات
cat config/aionic.conf | grep ai_model

# تأكد من تطابق اسم متغير البيئة
# إذا كان الإعداد يقول OPENAI_API_KEY، فيجب:
export OPENAI_API_KEY="sk-..."
```

### فشل طلب AI

```bash
# تأكد من إمكانية الوصول إلى رابط المزود
curl -v https://api.groq.com/openai/v1/models

# راجع سجلات السيرفر
tail -f logs/aionic.log

# تحقق من SSL (جرب verify_ssl = 0 في الإعدادات إذا كانت هناك مشكلة في الشهادات)
```

### تحسين الأداء

```ini
# في config/aionic.conf:
worker_threads = <عدد_أنوية_المعالج>   # طابق عدد أنوية CPU
max_connections = 10000              # زد للحمل العالي
keepalive_timeout = 60               # ابق الاتصالات حية لمدة أطول
enable_zero_copy = 1                 # فعّل لخدمة الملفات الثابتة
enable_smart_routing = 1             # وزّع الحمل على المزودين
```

### فشل البناء

```bash
# تأكد من تثبيت المتطلبات
dpkg -l | grep -E "build-essential|nasm|libcurl"

# نظف وأعد البناء
make clean && make

# جرب بدون تحسين
make debug
```

---

## 📜 الترخيص

MIT License — راجع [LICENSE](LICENSE).

---

## 🧬 المطور

**GUIAR OQBA** 🇩🇿
> *"بناء الجيل التالي من البنية التحتية للذكاء الاصطناعي — من القنطرة، الجزائر."*
