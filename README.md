# JrFeynman — Cafe / Restaurant Management

This repository contains:

| Path | Description |
|------|-------------|
| [`restaurant-pos/`](restaurant-pos/) | **RestoPulse** — modern **C++17 / macOS** full POS (recommended) |
| [`KafeYonetimSistemi-master/`](KafeYonetimSistemi-master/) | Original **Java Swing** cafe management system (legacy) |

---

## RestoPulse (C++ POS) — start here

Native Mac restaurant POS with:

- **Manager** and **Waiter** sessions (role-separated UI)
- 10-table floor map, orders, kitchen/bar tickets
- Recipe-based stock (event-sourced inventory ledger)
- Receipts, reports, product admin
- SQLite + optional Redis dual-engine database

### Quick start (macOS)

```bash
brew install cmake sqlite glfw
# optional: brew install redis hiredis && brew services start redis

cd restaurant-pos
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/restopulse --smoke   # headless test
./build/restopulse           # GUI
```

### Demo PINs

| PIN | Role |
|-----|------|
| `0000` | Manager — kitchen, stock, products, reports |
| `1111` / `2222` | Waiter — tables → orders → receipt → free table |

Full documentation: **[restaurant-pos/README.md](restaurant-pos/README.md)**  
Architecture: [restaurant-pos/docs/ARCHITECTURE.md](restaurant-pos/docs/ARCHITECTURE.md)  
Performance: [restaurant-pos/docs/PERFORMANCE.md](restaurant-pos/docs/PERFORMANCE.md)

---

## Legacy Java app

Educational Swing project under `KafeYonetimSistemi-master/` (menu text files, order export to `.txt`). Kept for reference; new development targets **RestoPulse**.

---

## License

Educational / portfolio project. Vendored Dear ImGui is MIT-licensed.
