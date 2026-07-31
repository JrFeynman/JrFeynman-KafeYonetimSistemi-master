# RestoPulse — Performance model & which systems run it fast

## Workload profile (this app)

| Dimension | Value for RestoPulse full POS |
|-----------|-------------------------------|
| Tables | 10 (fixed mock floor) |
| Concurrent open sessions | ≤ 10 |
| Menu size | ~40–50 SKUs (sizes expand drinks) |
| Ingredients | ~30 |
| Recipe lines / order | typically 5–40 ledger rows |
| Hot path ops | table status, cart UI, KDS refresh |
| Durable path ops | place order (ACID), pay, stock receive |
| Peak order rate (busy cafe) | ~1–3 orders/minute (<< 1/sec) |
| UI refresh | 60 FPS local; data refresh on action |

**Conclusion:** This is a **low-throughput, low-latency interactive** workload. It is not a high-QPS backend. Almost any modern laptop runs it “fast”; the interesting part is **tail latency** on order commit and **multi-client** floor sync.

## Architecture cost model

### Hot engine (RAM / optional Redis)

| Operation | Complexity | Expected latency |
|-----------|------------|------------------|
| Table status set/get | O(1) map | **&lt; 0.01 ms** in-process |
| Enqueue KDS ticket | O(1) | **&lt; 0.05 ms** |
| Redis SET/PUBLISH (local) | network localhost | **0.05–0.3 ms** |
| Redis over LAN | RTT-bound | **0.3–2 ms** typical |

### Ledger engine (SQLite WAL)

| Operation | Notes | Expected latency (SSD/NVMe) |
|-----------|-------|-----------------------------|
| Read menu + recipes (cold) | one-time | 5–20 ms |
| Read menu (warm page cache) | | 0.2–2 ms |
| `place_order` transaction | BEGIN IMMEDIATE + recipe explode + N ledger inserts + tickets | **1–8 ms** typical |
| `close_and_pay` | receipt + lines + session close | **1–6 ms** |
| Day report | aggregates with indexes | **2–15 ms** for a day |
| Full DB size (1 year busy cafe) | rough | **50–300 MB** |

SQLite settings used:

- `journal_mode=WAL` — readers don’t block writers long
- `synchronous=NORMAL` — safe for single-host POS
- `mmap_size=256MB` — faster reads
- `cache_size≈64MB`
- `busy_timeout=5000`
- single-writer mutex in process

### UI (Dear ImGui + OpenGL + GLFW)

| Item | Cost |
|------|------|
| GPU | trivial (2D UI) |
| CPU per frame | ~1–3% of one core at 60 FPS on Apple Silicon / modern x86 |
| RAM process | ~40–120 MB typical |

## End-to-end latency budget (order place)

```
UI click → validate cart     ~0.1 ms
Recipe explode + stock check ~0.2–1 ms
SQLite ACID commit           ~1–5 ms
Hot/KDS update (+ Redis)     ~0.05–1 ms
UI rebuild lists             ~0.5–2 ms
------------------------------------
Total perceived              ~2–10 ms  (feels instant)
```

Human-noticeable lag starts around **100 ms**. This design stays well under that on any machine that can compile C++17 and run OpenGL 3.2.

## Which systems run this app **fast**

### Tier S — Instant / overkill (recommended for developers & production Mac)

| System | Why |
|--------|-----|
| **Apple M1 / M2 / M3 / M4** (8GB+) | Unified memory, fast NVMe, excellent single-thread. Your current **M4 16GB** is Tier S. |
| **Intel/AMD laptop 2019+** (4+ cores, 8GB+, SSD) | Plenty for 10-table POS |
| **Mac mini / Studio** | Ideal always-on floor terminal |
| **Linux x86_64 NUC / mini PC** with SSD | Same code path (GLFW/OpenGL) |

**Expected:** UI 60 FPS locked, order commit &lt; 10 ms, Redis optional.

### Tier A — Fast enough for real service

| System | Notes |
|--------|-------|
| MacBook Air/Pro last ~8 years with SSD | Fine |
| Windows 10/11 + WSL2 or native build | Fine if OpenGL/GLFW works |
| Raspberry Pi 5 (8GB) + SSD | OK for single terminal; use in-process hot (skip Redis) |
| Older Intel i5 + HDD | UI OK; order commit may hit 20–40 ms on HDD — still usable |

### Tier B — Runs, but not “snappy”

| System | Bottleneck |
|--------|------------|
| HDD-only machines | SQLite fsync latency |
| &lt; 4GB RAM with heavy desktop | Memory pressure / swap |
| Network Redis on high-latency VPN | Multi-client events lag; use local Redis or in-process |
| Remote X11/VNC for UI | Frame latency, not DB |

### Tier F — Not suitable as primary POS terminal

| System | Why |
|--------|-----|
| Devices without OpenGL 3.2 / Metal-backed GL | UI won’t start |
| Pure microcontroller / no desktop OS | Wrong class of device |
| Shared spinning disk NAS for the SQLite file | Corruption risk + latency |

## Multi-terminal scaling (when Redis is on)

| Terminals | Redis host | Expected floor sync |
|-----------|------------|---------------------|
| 1 | off (in-process) | N/A — single process |
| 2–5 | same Mac / LAN mini | **&lt; 5 ms** event visibility |
| 10+ | dedicated Redis on LAN | still fine; SQLite remains single-writer — put **one writer service** or stick to one ledger host |

**Design rule:** One **Ledger writer** process (this app or a small server). Many **Hot** subscribers via Redis for KDS/floor mirrors.

For this delivery, the Mac app embeds both engines; Redis is an optional accelerator for multi-window sync.

## Resource recommendation (production-ish single cafe)

| Resource | Minimum | Comfortable | Notes |
|----------|---------|-------------|-------|
| CPU | 2 cores | 4+ cores / Apple Silicon | Single-thread matters more than core count |
| RAM | 4 GB | **8–16 GB** | OS + browser + POS |
| Storage | 1 GB free | SSD/NVMe 20GB+ free | DB + logs |
| Display | 1280×800 | **1400×900+** | Floor + order panels |
| Network | none | LAN if Redis multi-client | Offline-first OK |
| Redis | optional | local `redis-server` | Not required for smoke/single POS |

## Benchmark sketch (how to measure on your Mac)

```bash
# Build release
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# Functional + path correctness
./build/restopulse --smoke

# Optional Redis
brew services start redis
RESTOPULSE_REDIS_HOST=127.0.0.1 ./build/restopulse --smoke
```

Rough timing with `chrono` around `place_order` on Apple M-series is typically **sub-10 ms**.

## Capacity math (why 10 tables is trivial)

Assume worst case:

- 10 tables turn every 45 minutes → **~13 sessions/hour**
- 3 orders/session → **~40 orders/hour** → **0.01 orders/sec**
- Each order ~20 ledger writes → **0.2 writes/sec**

SQLite comfortably handles **thousands of writes/sec** on SSD.  
Redis handles **100k+ ops/sec** locally.

**You are ~4–5 orders of magnitude below engine limits.**  
“Fast” is therefore determined by **UI responsiveness and disk quality**, not database theory.

## Summary recommendation

| Goal | Machine |
|------|---------|
| Your Mac (M4 16GB) | **Excellent** — primary target |
| Cafe floor terminal | Any Apple Silicon Mac mini/laptop or Intel Mac 2019+ with SSD |
| Cheap dedicated box | Pi 5 8GB or used mini PC + SSD |
| Multi-station | One ledger host + Redis on LAN + KDS clients |

RestoPulse is engineered so the **special dual-engine DB** keeps the hot path in RAM and the durable path ACID — that is what makes it feel fast on ordinary hardware, not exotic servers.
