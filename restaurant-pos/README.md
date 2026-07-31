# RestoPulse — Restaurant POS (C++ / macOS)

Native **C++17** restaurant point-of-sale for Mac. Replaces the original Java *KafeYönetimSistemi* Swing app with a full POS-style system: table sessions, recipe-based stock, kitchen tickets, receipts, and **role-based Manager / Waiter sessions**.

| | |
|---|---|
| **Platform** | macOS (Apple Silicon & Intel) |
| **Language** | C++17 |
| **UI** | Dear ImGui + GLFW + OpenGL |
| **Durable DB** | SQLite (WAL, event-sourced inventory) |
| **Live state** | In-process hot store + optional Redis |
| **Roles** | Manager · Waiter only |

---

## Features

### Waiter session
Guided floor service flow:

1. **Open table** — seat guests (name + covers)  
2. **Take orders** — menu, modifiers, rush flag, stock-aware 86 list  
3. **Checkout / receipt** — cash or card, tax, tip, discount  
4. **Free table** — clear dirty table for the next party  
5. **My receipts** — reprint recent receipts  

Waiters do **not** see stock, kitchen, product admin, or reports.

### Manager session
Full back-of-house and floor control:

| Area | Capabilities |
|------|----------------|
| **Floor** | Live 10-table map, void orders, force free, open sessions |
| **Kitchen** | Kitchen + bar KDS tickets (Start → Done) |
| **Stock** | Ingredient levels, low-stock alerts, receive delivery, waste log |
| **Products** | Add menu items, optional recipe (stock deduct), enable/disable |
| **Receipts** | History, reprint, void |
| **Reports** | Day revenue, tax, tips, covers, top sellers, ingredient usage |
| **Take order** | Manager can also run the order/checkout flow |

### Shared restaurant logic
- **10 dining tables** with status: free → occupied → waiting → bill → dirty → free  
- **Recipe BOM** — each menu item can consume ingredients on order  
- **Atomic order commit** — lines + stock ledger + kitchen/bar tickets in one SQLite transaction  
- **Happy hour** pricing window (16:00–18:00 local)  
- **Combos, modifiers, allergens** in seed menu  
- **Loyalty** stamps/points on paid customer names (mock)  

---

## Special database: RestoPulse Dual-Engine

POS needs both **fast live state** and **ACID history**. One store is the wrong tool.

```
┌─────────────────────────────────────┐
│         UI  (Manager / Waiter)      │
└──────────────────┬──────────────────┘
                   │ DatabaseHub
        ┌──────────┴──────────┐
        ▼                     ▼
 ┌──────────────┐     ┌──────────────────┐
 │  HOT ENGINE  │     │  LEDGER ENGINE   │
 │  RAM (+Redis)│     │  SQLite WAL      │
 │              │     │                  │
 │ Table status │     │ Menu & recipes   │
 │ Sessions     │     │ Inventory ledger │
 │ KDS queue    │     │ Orders/receipts  │
 │ Low-stock    │     │ Reports / staff  │
 └──────────────┘     └──────────────────┘
```

| Engine | Role |
|--------|------|
| **Hot** | Sub-ms table/KDS updates; optional Redis keys `table:{id}:status`, pub/sub channels |
| **Ledger** | Source of truth for money & stock; append-only `inventory_ledger` (`order_consume`, `void_restock`, `receive`, `waste`, …) |

Details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) · Performance: [docs/PERFORMANCE.md](docs/PERFORMANCE.md)

---

## Requirements (macOS)

```bash
brew install cmake sqlite glfw
# optional multi-terminal live sync
brew install redis hiredis
```

- **Xcode Command Line Tools** (`xcode-select --install`)  
- **CMake** ≥ 3.16  
- **C++17** compiler (Apple Clang)  

---

## Build & run

```bash
git clone https://github.com/JrFeynman/RestoPulse.git
cd RestoPulse

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Headless correctness test (no window)
./build/restopulse --smoke

# GUI
./build/restopulse
```

### Optional Redis

```bash
brew services start redis
# or: redis-server --daemonize yes
./build/restopulse
```

| Environment variable | Default | Meaning |
|----------------------|---------|---------|
| `RESTOPULSE_REDIS_HOST` | `127.0.0.1` | Redis host |
| `RESTOPULSE_REDIS_PORT` | `6379` | Redis port |

If Redis is offline, the app still runs with the **in-process** hot engine.

Database file (auto-created & seeded on first run):

```
data/restopulse.db
```

---

## Login PINs (demo)

| PIN | Role | Access |
|-----|------|--------|
| **0000** | Manager | Kitchen, stock, products, floor, reports, take order |
| **1111** | Waiter | Service + receipts |
| **2222** | Waiter | Service + receipts |

Only **manager** and **waiter** sessions are allowed.

---

## Project layout

```
RestoPulse/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── ARCHITECTURE.md      # Dual-engine design
│   └── PERFORMANCE.md       # Latency model & hardware tiers
├── data/                    # SQLite DB (generated, gitignored)
├── src/
│   ├── main.cpp             # Entry, --smoke mode
│   ├── core/types.hpp       # Domain types
│   ├── db/
│   │   ├── ledger.*         # SQLite ledger + seed + products
│   │   ├── hot_store.*      # RAM hot engine + Redis bridge
│   │   └── hub.*            # Unified facade
│   ├── services/pos_app.*   # Role-aware POS API
│   └── ui/app_ui.*          # ImGui Manager / Waiter shells
└── third_party/imgui/       # Vendored Dear ImGui
```

---

## Typical demo walkthrough

### As waiter (`1111`)
1. Start session → **Service** tab  
2. Click free table **T1** → name + guests → **Open table**  
3. Add **Latte** + **Sandviç** → **Send order**  
4. **Checkout / Receipt** → Card → pay  
5. **Free table now** when cleared  

### As manager (`0000`)
1. **Kitchen** — Start / Done tickets  
2. **Stock** — watch levels drop; receive a delivery  
3. **Products** — add “Iced Mocha”, link milk recipe, save  
4. **Reports** — day revenue & top sellers  

---

## Performance (short)

A **10-table** cafe is a light load. Order commits are typically **1–8 ms** on SSD/NVMe. Any recent Mac with 8 GB RAM runs it comfortably; **Apple Silicon (M1–M4)** is ideal.

See [docs/PERFORMANCE.md](docs/PERFORMANCE.md) for hardware tiers and capacity math.

---

## Origin

Rewritten from the educational Java project **KafeYönetimSistemi** (Swing UI, text-file menus/orders) into a production-style POS architecture suitable for teaching systems design: dual stores, event-sourced inventory, and role-separated UX.

Related legacy repo: [JrFeynman-KafeYonetimSistemi-master](https://github.com/JrFeynman/JrFeynman-KafeYonetimSistemi-master)

---

## License

Educational / personal project. Dear ImGui is under its own MIT license (`third_party/imgui`). Use and extend freely for learning and portfolio work.

---

## Troubleshooting

| Issue | Fix |
|-------|-----|
| `glfwInit failed` | Install GLFW: `brew install glfw` |
| `sqlite3 not found` | `brew install sqlite` then reconfigure CMake |
| Build cache points at old path | `rm -rf build && cmake -S . -B build && cmake --build build -j` |
| UI looks outdated after pull | Rebuild and restart `./build/restopulse` |
| Want a clean DB | Delete `data/restopulse.db*` and relaunch |

```bash
# Clean rebuild
rm -rf build data/restopulse.db*
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/restopulse --smoke
```
