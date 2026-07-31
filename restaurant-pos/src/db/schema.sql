-- RestoPulse Ledger Engine schema
-- Event-sourced inventory + durable POS history
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS staff (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    role        TEXT NOT NULL CHECK(role IN ('waiter','cashier','kitchen','manager')),
    pin_hash    TEXT NOT NULL,
    pin_salt    TEXT NOT NULL,
    active      INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS dining_tables (
    id          INTEGER PRIMARY KEY,
    label       TEXT NOT NULL,
    seats       INTEGER NOT NULL DEFAULT 4,
    pos_x       REAL NOT NULL DEFAULT 0,
    pos_y       REAL NOT NULL DEFAULT 0,
    status      TEXT NOT NULL DEFAULT 'free'
        CHECK(status IN ('free','occupied','ordering','waiting','bill','dirty'))
);

CREATE TABLE IF NOT EXISTS ingredients (
    id              INTEGER PRIMARY KEY,
    sku             TEXT NOT NULL UNIQUE,
    name            TEXT NOT NULL,
    unit            TEXT NOT NULL,          -- g, ml, pcs, bottle
    stock_qty       REAL NOT NULL DEFAULT 0,
    reorder_level   REAL NOT NULL DEFAULT 0,
    cost_per_unit   REAL NOT NULL DEFAULT 0,
    allergen_flags  TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS menu_categories (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL,
    sort  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS menu_items (
    id              INTEGER PRIMARY KEY,
    category_id     INTEGER NOT NULL REFERENCES menu_categories(id),
    name            TEXT NOT NULL,
    base_price      REAL NOT NULL,
    item_type       TEXT NOT NULL CHECK(item_type IN ('food','drink','combo')),
    size_label      TEXT NOT NULL DEFAULT '',
    available       INTEGER NOT NULL DEFAULT 1,
    prep_seconds    INTEGER NOT NULL DEFAULT 300,
    allergens       TEXT NOT NULL DEFAULT '',
    happy_hour_price REAL,                 -- NULL = no happy hour
    is_combo        INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS recipes (
    id           INTEGER PRIMARY KEY,
    menu_item_id INTEGER NOT NULL UNIQUE REFERENCES menu_items(id) ON DELETE CASCADE,
    name         TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS recipe_lines (
    id              INTEGER PRIMARY KEY,
    recipe_id       INTEGER NOT NULL REFERENCES recipes(id) ON DELETE CASCADE,
    ingredient_id   INTEGER NOT NULL REFERENCES ingredients(id),
    qty             REAL NOT NULL,
    UNIQUE(recipe_id, ingredient_id)
);

CREATE TABLE IF NOT EXISTS modifiers (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL,
    price_delta     REAL NOT NULL DEFAULT 0,
    ingredient_id   INTEGER REFERENCES ingredients(id),
    ingredient_qty  REAL NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS menu_item_modifiers (
    menu_item_id INTEGER NOT NULL REFERENCES menu_items(id) ON DELETE CASCADE,
    modifier_id  INTEGER NOT NULL REFERENCES modifiers(id) ON DELETE CASCADE,
    PRIMARY KEY(menu_item_id, modifier_id)
);

CREATE TABLE IF NOT EXISTS combo_components (
    combo_item_id     INTEGER NOT NULL REFERENCES menu_items(id) ON DELETE CASCADE,
    component_item_id INTEGER NOT NULL REFERENCES menu_items(id),
    PRIMARY KEY(combo_item_id, component_item_id)
);

-- Open / closed table sessions
CREATE TABLE IF NOT EXISTS sessions (
    id            INTEGER PRIMARY KEY,
    table_id      INTEGER NOT NULL REFERENCES dining_tables(id),
    opened_at     TEXT NOT NULL,
    closed_at     TEXT,
    covers        INTEGER NOT NULL DEFAULT 2,
    customer_name TEXT NOT NULL DEFAULT '',
    waiter_id     INTEGER REFERENCES staff(id),
    status        TEXT NOT NULL DEFAULT 'open'
        CHECK(status IN ('open','closed','void')),
    notes         TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS orders (
    id            INTEGER PRIMARY KEY,
    session_id    INTEGER NOT NULL REFERENCES sessions(id),
    placed_at     TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'placed'
        CHECK(status IN ('placed','preparing','ready','served','void')),
    priority      INTEGER NOT NULL DEFAULT 0,
    placed_by     INTEGER REFERENCES staff(id),
    fire_at       TEXT,                    -- course hold
    note          TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS order_lines (
    id              INTEGER PRIMARY KEY,
    order_id        INTEGER NOT NULL REFERENCES orders(id) ON DELETE CASCADE,
    menu_item_id    INTEGER NOT NULL REFERENCES menu_items(id),
    qty             INTEGER NOT NULL DEFAULT 1,
    unit_price      REAL NOT NULL,
    status          TEXT NOT NULL DEFAULT 'placed'
        CHECK(status IN ('placed','preparing','ready','served','void')),
    note            TEXT NOT NULL DEFAULT '',
    seat_no         INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS order_line_modifiers (
    order_line_id INTEGER NOT NULL REFERENCES order_lines(id) ON DELETE CASCADE,
    modifier_id   INTEGER NOT NULL REFERENCES modifiers(id),
    PRIMARY KEY(order_line_id, modifier_id)
);

-- Cached balances (source of truth for rebuild is inventory_ledger)
CREATE TABLE IF NOT EXISTS inventory_balances (
    ingredient_id INTEGER PRIMARY KEY REFERENCES ingredients(id),
    qty           REAL NOT NULL,
    updated_at    TEXT NOT NULL
);

-- Event-sourced inventory ledger (append-only)
CREATE TABLE IF NOT EXISTS inventory_ledger (
    id              INTEGER PRIMARY KEY,
    ts              TEXT NOT NULL,
    ingredient_id   INTEGER NOT NULL REFERENCES ingredients(id),
    delta           REAL NOT NULL,          -- negative = consume
    reason          TEXT NOT NULL
        CHECK(reason IN ('seed','order_consume','void_restock','waste','receive','adjustment')),
    ref_type        TEXT NOT NULL DEFAULT '',  -- order / receipt / manual
    ref_id          INTEGER,
    note            TEXT NOT NULL DEFAULT '',
    staff_id        INTEGER REFERENCES staff(id)
);

CREATE TABLE IF NOT EXISTS receipts (
    id              INTEGER PRIMARY KEY,
    session_id      INTEGER NOT NULL REFERENCES sessions(id),
    issued_at       TEXT NOT NULL,
    subtotal        REAL NOT NULL,
    tax             REAL NOT NULL,
    service_charge  REAL NOT NULL DEFAULT 0,
    discount        REAL NOT NULL DEFAULT 0,
    tip             REAL NOT NULL DEFAULT 0,
    total           REAL NOT NULL,
    status          TEXT NOT NULL DEFAULT 'issued'
        CHECK(status IN ('issued','void','refunded')),
    receipt_no      TEXT NOT NULL UNIQUE,
    cashier_id      INTEGER REFERENCES staff(id),
    print_count     INTEGER NOT NULL DEFAULT 1,
    customer_name   TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS receipt_lines (
    id            INTEGER PRIMARY KEY,
    receipt_id    INTEGER NOT NULL REFERENCES receipts(id) ON DELETE CASCADE,
    description   TEXT NOT NULL,
    qty           INTEGER NOT NULL,
    unit_price    REAL NOT NULL,
    line_total    REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS receipt_payments (
    id          INTEGER PRIMARY KEY,
    receipt_id  INTEGER NOT NULL REFERENCES receipts(id) ON DELETE CASCADE,
    method      TEXT NOT NULL CHECK(method IN ('cash','card','mixed','other')),
    amount      REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS shifts (
    id            INTEGER PRIMARY KEY,
    staff_id      INTEGER NOT NULL REFERENCES staff(id),
    opened_at     TEXT NOT NULL,
    closed_at     TEXT,
    opening_float REAL NOT NULL DEFAULT 0,
    closing_cash  REAL,
    notes         TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS loyalty (
    id            INTEGER PRIMARY KEY,
    customer_name TEXT NOT NULL UNIQUE,
    stamps        INTEGER NOT NULL DEFAULT 0,
    points        INTEGER NOT NULL DEFAULT 0,
    updated_at    TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS waitlist (
    id            INTEGER PRIMARY KEY,
    name          TEXT NOT NULL,
    party_size    INTEGER NOT NULL,
    phone         TEXT NOT NULL DEFAULT '',
    created_at    TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'waiting'
        CHECK(status IN ('waiting','seated','cancelled','no_show')),
    estimated_wait_min INTEGER NOT NULL DEFAULT 15
);

CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS kitchen_tickets (
    id            INTEGER PRIMARY KEY,
    order_id      INTEGER NOT NULL REFERENCES orders(id),
    station       TEXT NOT NULL CHECK(station IN ('kitchen','bar')),
    created_at    TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'queued'
        CHECK(status IN ('queued','in_progress','done','cancelled')),
    priority      INTEGER NOT NULL DEFAULT 0
);

-- Indexes for hot queries
CREATE INDEX IF NOT EXISTS idx_sessions_open ON sessions(status, table_id);
CREATE INDEX IF NOT EXISTS idx_orders_session ON orders(session_id, status);
CREATE INDEX IF NOT EXISTS idx_order_lines_order ON order_lines(order_id);
CREATE INDEX IF NOT EXISTS idx_ledger_ing ON inventory_ledger(ingredient_id, ts);
CREATE INDEX IF NOT EXISTS idx_receipts_day ON receipts(issued_at);
CREATE INDEX IF NOT EXISTS idx_tickets_status ON kitchen_tickets(status, station);
CREATE INDEX IF NOT EXISTS idx_menu_cat ON menu_items(category_id, available);
