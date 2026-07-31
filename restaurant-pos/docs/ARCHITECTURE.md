# RestoPulse POS — Architecture

## Product scope

Full POS-style restaurant management for macOS (C++17):

- Floor map with **10 tables** and live session lifecycle
- Menu, modifiers, combos, allergens
- Recipe-based inventory with automatic deduction / restock on void
- Kitchen display (KDS) ticket queue
- Receipt manager (issue, reprint, void, split, Z-report)
- Staff PIN roles (waiter / cashier / kitchen / manager)
- Analytics: top sellers, turnover, ingredient usage
- Happy-hour pricing, waitlist, loyalty stamps (mock)

## Special database: **RestoPulse Dual-Engine**

POS needs both **sub-millisecond live state** and **ACID durable history**. One store is the wrong tool; we use two coordinated engines behind one API (`DatabaseHub`).

```
┌─────────────────────────────────────────────────────────────┐
│                     Application (UI / services)              │
└────────────────────────────┬────────────────────────────────┘
                             │ DatabaseHub (single facade)
              ┌──────────────┴──────────────┐
              ▼                             ▼
   ┌─────────────────────┐       ┌──────────────────────────┐
   │  HOT ENGINE         │       │  LEDGER ENGINE           │
   │  (RestoHot)         │       │  (RestoLedger / SQLite)  │
   │                     │       │                          │
   │  • Open table       │       │  • Menu & recipes        │
   │    sessions         │       │  • Inventory balances    │
   │  • Live order lines │       │  • Inventory ledger      │
   │  • Kitchen tickets  │       │    (event-sourced)       │
   │  • Staff presence   │       │  • Closed receipts       │
   │  • Pub/sub events   │       │  • Staff & shifts        │
   │                     │       │  • Daily Z-reports       │
   │  Backends:          │       │  • Analytics aggregates  │
   │   1) In-process RAM │       │                          │
   │   2) Redis (optional│       │  WAL + prepared stmts    │
   │      multi-client)  │       │  single-writer queue     │
   └─────────────────────┘       └──────────────────────────┘
```

### Why this is “special”

1. **Inventory is event-sourced**  
   Stock never becomes “random absolute numbers only.” Every change is a ledger row:
   `order_consume | void_restock | waste | receive | adjustment`  
   Balance = seed + Σ(deltas). Rebuild-safe and audit-ready.

2. **Atomic order commit**  
   Placing an order runs one SQLite transaction:
   - insert order lines
   - explode recipes → ingredient lines
   - append ledger deltas
   - update balance cache
   - emit kitchen tickets to Hot engine  
   Either everything commits or nothing does (no ghost stock).

3. **Hot path never blocks on disk for floor UX**  
   Table status, open sessions, and KDS tickets live in RAM (and optionally Redis).  
   Crash recovery: reopen app → Ledger reloads open sessions flagged `status=open`.

4. **Redis is an accelerator, not a hard dependency**  
   Same key layout works in-process. When Redis is up, multiple windows/devices share live floor state via pub/sub channels:
   - `restopulse:events:table`
   - `restopulse:events:kitchen`
   - `restopulse:events:stock`

5. **SQLite tuned for POS**  
   - `journal_mode=WAL`, `synchronous=NORMAL`  
   - `temp_store=MEMORY`, `mmap_size=256MB`  
   - covering indexes on receipts, ledger, order_lines  
   - busy timeout + serialized writes

### Key namespaces (Hot / Redis)

| Key pattern | Meaning |
|-------------|---------|
| `table:{id}:session` | JSON open session |
| `table:{id}:status` | free/occupied/ordering/waiting/bill/dirty |
| `kitchen:queue` | list of open ticket IDs |
| `ticket:{id}` | ticket JSON |
| `stock:alert` | set of low-stock ingredient IDs |
| `staff:active` | current PIN user |

### Ledger tables (SQLite)

See `src/db/schema.sql` — core tables:

`staff`, `ingredients`, `menu_items`, `recipes`, `recipe_lines`, `modifiers`,  
`tables`, `sessions`, `orders`, `order_lines`, `inventory_balances`,  
`inventory_ledger`, `receipts`, `receipt_payments`, `shifts`, `loyalty`, `waitlist`, `settings`.

## Runtime layout

```
restaurant-pos/
  src/core      domain types
  src/db        Hot + Ledger + DatabaseHub
  src/services  POS, Kitchen, Inventory, Reports, Auth
  src/ui        Dear ImGui floor / order / kitchen / stock / receipts
  data/         SQLite file + seed JSON
  docs/         architecture + performance
```

## Security (mock POS)

- Staff PIN hashed with SHA-256 + salt (demo only; not production KMS)
- Manager required for voids, discounts > 15%, free items
- Receipt void appends compensating ledger rows (restock)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/restopulse
```
