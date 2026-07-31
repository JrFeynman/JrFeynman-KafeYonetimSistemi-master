#include "db/ledger.hpp"
#include "util/sha256.hpp"
#include <cstring>
#include <fstream>
#include <sstream>
#include <cmath>
#include <map>

namespace rp {

namespace {
std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}

Ledger::Ledger(std::string db_path) : path_(std::move(db_path)) {}
Ledger::~Ledger() { close(); }

void Ledger::open() {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_) return;
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "open failed";
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        throw std::runtime_error("SQLite open: " + err);
    }
    migrate_and_tune();
}

void Ledger::close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Ledger::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), cb_noop, nullptr, &err) != SQLITE_OK) {
        std::string e = err ? err : "exec error";
        sqlite3_free(err);
        throw std::runtime_error(e);
    }
}

void Ledger::exec_script(const std::string& sql) { exec(sql); }

void Ledger::migrate_and_tune() {
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    exec("PRAGMA temp_store=MEMORY;");
    exec("PRAGMA mmap_size=268435456;");
    exec("PRAGMA busy_timeout=5000;");
    exec("PRAGMA cache_size=-64000;"); // ~64MB

    // schema embedded for reliability
    static const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS staff (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL,
    role TEXT NOT NULL, pin_hash TEXT NOT NULL, pin_salt TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1);
CREATE TABLE IF NOT EXISTS dining_tables (
    id INTEGER PRIMARY KEY, label TEXT NOT NULL, seats INTEGER NOT NULL DEFAULT 4,
    pos_x REAL NOT NULL DEFAULT 0, pos_y REAL NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'free');
CREATE TABLE IF NOT EXISTS ingredients (
    id INTEGER PRIMARY KEY, sku TEXT NOT NULL UNIQUE, name TEXT NOT NULL, unit TEXT NOT NULL,
    stock_qty REAL NOT NULL DEFAULT 0, reorder_level REAL NOT NULL DEFAULT 0,
    cost_per_unit REAL NOT NULL DEFAULT 0, allergen_flags TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS menu_categories (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL, sort INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS menu_items (
    id INTEGER PRIMARY KEY, category_id INTEGER NOT NULL, name TEXT NOT NULL, base_price REAL NOT NULL,
    item_type TEXT NOT NULL, size_label TEXT NOT NULL DEFAULT '', available INTEGER NOT NULL DEFAULT 1,
    prep_seconds INTEGER NOT NULL DEFAULT 300, allergens TEXT NOT NULL DEFAULT '',
    happy_hour_price REAL, is_combo INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS recipes (
    id INTEGER PRIMARY KEY, menu_item_id INTEGER NOT NULL UNIQUE, name TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS recipe_lines (
    id INTEGER PRIMARY KEY, recipe_id INTEGER NOT NULL, ingredient_id INTEGER NOT NULL, qty REAL NOT NULL,
    UNIQUE(recipe_id, ingredient_id));
CREATE TABLE IF NOT EXISTS modifiers (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL, price_delta REAL NOT NULL DEFAULT 0,
    ingredient_id INTEGER, ingredient_qty REAL NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS menu_item_modifiers (
    menu_item_id INTEGER NOT NULL, modifier_id INTEGER NOT NULL,
    PRIMARY KEY(menu_item_id, modifier_id));
CREATE TABLE IF NOT EXISTS combo_components (
    combo_item_id INTEGER NOT NULL, component_item_id INTEGER NOT NULL,
    PRIMARY KEY(combo_item_id, component_item_id));
CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY, table_id INTEGER NOT NULL, opened_at TEXT NOT NULL, closed_at TEXT,
    covers INTEGER NOT NULL DEFAULT 2, customer_name TEXT NOT NULL DEFAULT '',
    waiter_id INTEGER, status TEXT NOT NULL DEFAULT 'open', notes TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS orders (
    id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL, placed_at TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'placed', priority INTEGER NOT NULL DEFAULT 0,
    placed_by INTEGER, fire_at TEXT, note TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS order_lines (
    id INTEGER PRIMARY KEY, order_id INTEGER NOT NULL, menu_item_id INTEGER NOT NULL,
    qty INTEGER NOT NULL DEFAULT 1, unit_price REAL NOT NULL, status TEXT NOT NULL DEFAULT 'placed',
    note TEXT NOT NULL DEFAULT '', seat_no INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS order_line_modifiers (
    order_line_id INTEGER NOT NULL, modifier_id INTEGER NOT NULL,
    PRIMARY KEY(order_line_id, modifier_id));
CREATE TABLE IF NOT EXISTS inventory_balances (
    ingredient_id INTEGER PRIMARY KEY, qty REAL NOT NULL, updated_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS inventory_ledger (
    id INTEGER PRIMARY KEY, ts TEXT NOT NULL, ingredient_id INTEGER NOT NULL, delta REAL NOT NULL,
    reason TEXT NOT NULL, ref_type TEXT NOT NULL DEFAULT '', ref_id INTEGER,
    note TEXT NOT NULL DEFAULT '', staff_id INTEGER);
CREATE TABLE IF NOT EXISTS receipts (
    id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL, issued_at TEXT NOT NULL,
    subtotal REAL NOT NULL, tax REAL NOT NULL, service_charge REAL NOT NULL DEFAULT 0,
    discount REAL NOT NULL DEFAULT 0, tip REAL NOT NULL DEFAULT 0, total REAL NOT NULL,
    status TEXT NOT NULL DEFAULT 'issued', receipt_no TEXT NOT NULL UNIQUE,
    cashier_id INTEGER, print_count INTEGER NOT NULL DEFAULT 1, customer_name TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS receipt_lines (
    id INTEGER PRIMARY KEY, receipt_id INTEGER NOT NULL, description TEXT NOT NULL,
    qty INTEGER NOT NULL, unit_price REAL NOT NULL, line_total REAL NOT NULL);
CREATE TABLE IF NOT EXISTS receipt_payments (
    id INTEGER PRIMARY KEY, receipt_id INTEGER NOT NULL, method TEXT NOT NULL, amount REAL NOT NULL);
CREATE TABLE IF NOT EXISTS shifts (
    id INTEGER PRIMARY KEY, staff_id INTEGER NOT NULL, opened_at TEXT NOT NULL, closed_at TEXT,
    opening_float REAL NOT NULL DEFAULT 0, closing_cash REAL, notes TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS loyalty (
    id INTEGER PRIMARY KEY, customer_name TEXT NOT NULL UNIQUE, stamps INTEGER NOT NULL DEFAULT 0,
    points INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS waitlist (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL, party_size INTEGER NOT NULL,
    phone TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'waiting',
    estimated_wait_min INTEGER NOT NULL DEFAULT 15);
CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS kitchen_tickets (
    id INTEGER PRIMARY KEY, order_id INTEGER NOT NULL, station TEXT NOT NULL,
    created_at TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'queued', priority INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS idx_sessions_open ON sessions(status, table_id);
CREATE INDEX IF NOT EXISTS idx_orders_session ON orders(session_id, status);
CREATE INDEX IF NOT EXISTS idx_order_lines_order ON order_lines(order_id);
CREATE INDEX IF NOT EXISTS idx_ledger_ing ON inventory_ledger(ingredient_id, ts);
CREATE INDEX IF NOT EXISTS idx_receipts_day ON receipts(issued_at);
CREATE INDEX IF NOT EXISTS idx_tickets_status ON kitchen_tickets(status, station);
CREATE INDEX IF NOT EXISTS idx_menu_cat ON menu_items(category_id, available);
)SQL";
    exec(schema);
}

void Ledger::begin() { exec("BEGIN IMMEDIATE;"); }
void Ledger::commit() { exec("COMMIT;"); }
void Ledger::rollback() {
    char* err = nullptr;
    sqlite3_exec(db_, "ROLLBACK;", cb_noop, nullptr, &err);
    if (err) sqlite3_free(err);
}
int Ledger::last_id() { return static_cast<int>(sqlite3_last_insert_rowid(db_)); }

bool Ledger::is_seeded() const {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT value FROM meta WHERE key='seeded'", -1, &st, nullptr);
    bool ok = false;
    if (sqlite3_step(st) == SQLITE_ROW) ok = true;
    sqlite3_finalize(st);
    return ok;
}

void Ledger::set_setting(const std::string& key, const std::string& value) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::string Ledger::setting(const std::string& key, const std::string& def) const {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT value FROM settings WHERE key=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string v = def;
    if (sqlite3_step(st) == SQLITE_ROW) v = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return v;
}

bool Ledger::is_happy_hour() const {
    // Simple: happy hour 16:00-18:00 local
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    const int mins = tm.tm_hour * 60 + tm.tm_min;
    return mins >= 16 * 60 && mins < 18 * 60;
}

double Ledger::price_for(const MenuItem& item, bool happy_hour) const {
    if (happy_hour && item.happy_hour_price) return *item.happy_hour_price;
    return item.base_price;
}

std::optional<Staff> Ledger::login_pin(const std::string& pin) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,role,pin_hash,pin_salt,active FROM staff WHERE active=1", -1, &st, nullptr);
    std::optional<Staff> found;
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::string hash = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        std::string salt = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        if (sha256::hash_pin(pin, salt) == hash) {
            Staff s;
            s.id = sqlite3_column_int(st, 0);
            s.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            s.role = role_from(reinterpret_cast<const char*>(sqlite3_column_text(st, 2)));
            s.active = sqlite3_column_int(st, 5) != 0;
            found = s;
            break;
        }
    }
    sqlite3_finalize(st);
    return found;
}

std::vector<Staff> Ledger::list_staff() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Staff> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,role,active FROM staff ORDER BY id", -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Staff s;
        s.id = sqlite3_column_int(st, 0);
        s.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        s.role = role_from(reinterpret_cast<const char*>(sqlite3_column_text(st, 2)));
        s.active = sqlite3_column_int(st, 3) != 0;
        out.push_back(s);
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<DiningTable> Ledger::list_tables() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<DiningTable> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT t.id,t.label,t.seats,t.pos_x,t.pos_y,t.status,"
        " (SELECT s.id FROM sessions s WHERE s.table_id=t.id AND s.status='open' LIMIT 1),"
        " (SELECT s.covers FROM sessions s WHERE s.table_id=t.id AND s.status='open' LIMIT 1),"
        " (SELECT s.customer_name FROM sessions s WHERE s.table_id=t.id AND s.status='open' LIMIT 1)"
        " FROM dining_tables t ORDER BY t.id", -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        DiningTable t;
        t.id = sqlite3_column_int(st, 0);
        t.label = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        t.seats = sqlite3_column_int(st, 2);
        t.pos_x = static_cast<float>(sqlite3_column_double(st, 3));
        t.pos_y = static_cast<float>(sqlite3_column_double(st, 4));
        t.status = table_status_from(reinterpret_cast<const char*>(sqlite3_column_text(st, 5)));
        if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
            t.open_session_id = sqlite3_column_int(st, 6);
            t.covers = sqlite3_column_int(st, 7);
            if (sqlite3_column_type(st, 8) != SQLITE_NULL)
                t.customer_name = reinterpret_cast<const char*>(sqlite3_column_text(st, 8));
        }
        // running total
        if (t.open_session_id) {
            sqlite3_stmt* st2 = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT COALESCE(SUM(ol.qty*ol.unit_price),0) FROM order_lines ol "
                "JOIN orders o ON o.id=ol.order_id WHERE o.session_id=? AND ol.status!='void' AND o.status!='void'",
                -1, &st2, nullptr);
            sqlite3_bind_int(st2, 1, t.open_session_id);
            if (sqlite3_step(st2) == SQLITE_ROW) t.running_total = sqlite3_column_double(st2, 0);
            sqlite3_finalize(st2);
        }
        out.push_back(t);
    }
    sqlite3_finalize(st);
    return out;
}

void Ledger::set_table_status(int table_id, TableStatus st) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE dining_tables SET status=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, to_string(st), -1, SQLITE_STATIC);
    sqlite3_bind_int(s, 2, table_id);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

int Ledger::open_session(int table_id, int covers, const std::string& customer, int waiter_id) {
    std::lock_guard<std::mutex> lock(mu_);
    // ensure free or dirty -> open
    begin();
    try {
        sqlite3_stmt* chk = nullptr;
        sqlite3_prepare_v2(db_, "SELECT id FROM sessions WHERE table_id=? AND status='open'", -1, &chk, nullptr);
        sqlite3_bind_int(chk, 1, table_id);
        if (sqlite3_step(chk) == SQLITE_ROW) {
            int existing = sqlite3_column_int(chk, 0);
            sqlite3_finalize(chk);
            commit();
            return existing;
        }
        sqlite3_finalize(chk);

        const std::string ts = now_iso();
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT INTO sessions(table_id,opened_at,covers,customer_name,waiter_id,status) VALUES(?,?,?,?,?,'open')",
            -1, &st, nullptr);
        sqlite3_bind_int(st, 1, table_id);
        sqlite3_bind_text(st, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, covers);
        sqlite3_bind_text(st, 4, customer.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 5, waiter_id);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); throw std::runtime_error("open session"); }
        sqlite3_finalize(st);
        int sid = last_id();
        sqlite3_stmt* u = nullptr;
        sqlite3_prepare_v2(db_, "UPDATE dining_tables SET status='occupied' WHERE id=?", -1, &u, nullptr);
        sqlite3_bind_int(u, 1, table_id);
        sqlite3_step(u);
        sqlite3_finalize(u);
        commit();
        return sid;
    } catch (...) {
        rollback();
        throw;
    }
}

void Ledger::close_session(int session_id) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string ts = now_iso();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE sessions SET status='closed', closed_at=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, session_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::optional<Session> Ledger::open_session_for_table(int table_id) {
    int sid = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "SELECT id FROM sessions WHERE table_id=? AND status='open' LIMIT 1", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, table_id);
        if (sqlite3_step(st) == SQLITE_ROW) sid = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (!sid) return std::nullopt;
    return load_session(sid);
}

Session Ledger::load_session(int session_id) {
    std::lock_guard<std::mutex> lock(mu_);
    Session s;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,table_id,opened_at,COALESCE(closed_at,''),covers,customer_name,COALESCE(waiter_id,0),status,notes FROM sessions WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, session_id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        throw std::runtime_error("session not found");
    }
    s.id = sqlite3_column_int(st, 0);
    s.table_id = sqlite3_column_int(st, 1);
    s.opened_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    s.closed_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
    s.covers = sqlite3_column_int(st, 4);
    s.customer_name = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
    s.waiter_id = sqlite3_column_int(st, 6);
    s.status = reinterpret_cast<const char*>(sqlite3_column_text(st, 7));
    s.notes = reinterpret_cast<const char*>(sqlite3_column_text(st, 8));
    sqlite3_finalize(st);

    sqlite3_stmt* o = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,session_id,placed_at,status,priority,COALESCE(placed_by,0),note FROM orders WHERE session_id=? ORDER BY id",
        -1, &o, nullptr);
    sqlite3_bind_int(o, 1, session_id);
    while (sqlite3_step(o) == SQLITE_ROW) {
        Order ord;
        ord.id = sqlite3_column_int(o, 0);
        ord.session_id = sqlite3_column_int(o, 1);
        ord.placed_at = reinterpret_cast<const char*>(sqlite3_column_text(o, 2));
        ord.status = order_status_from(reinterpret_cast<const char*>(sqlite3_column_text(o, 3)));
        ord.priority = sqlite3_column_int(o, 4);
        ord.placed_by = sqlite3_column_int(o, 5);
        ord.note = reinterpret_cast<const char*>(sqlite3_column_text(o, 6));

        sqlite3_stmt* l = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT ol.id,ol.menu_item_id,m.name,ol.qty,ol.unit_price,ol.status,ol.note,ol.seat_no "
            "FROM order_lines ol JOIN menu_items m ON m.id=ol.menu_item_id WHERE ol.order_id=?",
            -1, &l, nullptr);
        sqlite3_bind_int(l, 1, ord.id);
        while (sqlite3_step(l) == SQLITE_ROW) {
            OrderLine line;
            line.id = sqlite3_column_int(l, 0);
            line.menu_item_id = sqlite3_column_int(l, 1);
            line.name = reinterpret_cast<const char*>(sqlite3_column_text(l, 2));
            line.qty = sqlite3_column_int(l, 3);
            line.unit_price = sqlite3_column_double(l, 4);
            line.status = order_status_from(reinterpret_cast<const char*>(sqlite3_column_text(l, 5)));
            line.note = reinterpret_cast<const char*>(sqlite3_column_text(l, 6));
            line.seat_no = sqlite3_column_int(l, 7);
            ord.lines.push_back(line);
        }
        sqlite3_finalize(l);
        s.orders.push_back(std::move(ord));
    }
    sqlite3_finalize(o);
    return s;
}

std::vector<std::pair<int,std::string>> Ledger::categories() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::pair<int,std::string>> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name FROM menu_categories ORDER BY sort,id", -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW)
        out.emplace_back(sqlite3_column_int(st, 0), reinterpret_cast<const char*>(sqlite3_column_text(st, 1)));
    sqlite3_finalize(st);
    return out;
}

void Ledger::load_recipe_into(MenuItem& item) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT i.id,i.name,i.unit,rl.qty FROM recipe_lines rl "
        "JOIN recipes r ON r.id=rl.recipe_id JOIN ingredients i ON i.id=rl.ingredient_id "
        "WHERE r.menu_item_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, item.id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        RecipeLine rl;
        rl.ingredient_id = sqlite3_column_int(st, 0);
        rl.ingredient_name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        rl.unit = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        rl.qty = sqlite3_column_double(st, 3);
        item.recipe.push_back(rl);
    }
    sqlite3_finalize(st);

    sqlite3_stmt* m = nullptr;
    sqlite3_prepare_v2(db_, "SELECT modifier_id FROM menu_item_modifiers WHERE menu_item_id=?", -1, &m, nullptr);
    sqlite3_bind_int(m, 1, item.id);
    while (sqlite3_step(m) == SQLITE_ROW) item.modifier_ids.push_back(sqlite3_column_int(m, 0));
    sqlite3_finalize(m);

    if (item.is_combo) {
        sqlite3_stmt* c = nullptr;
        sqlite3_prepare_v2(db_, "SELECT component_item_id FROM combo_components WHERE combo_item_id=?", -1, &c, nullptr);
        sqlite3_bind_int(c, 1, item.id);
        while (sqlite3_step(c) == SQLITE_ROW) item.combo_component_ids.push_back(sqlite3_column_int(c, 0));
        sqlite3_finalize(c);
    }
}

std::vector<MenuItem> Ledger::menu_items(bool only_available) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<MenuItem> out;
    std::string sql =
        "SELECT m.id,m.category_id,c.name,m.name,m.base_price,m.item_type,m.size_label,m.available,"
        "m.prep_seconds,m.allergens,m.happy_hour_price,m.is_combo FROM menu_items m "
        "JOIN menu_categories c ON c.id=m.category_id";
    if (only_available) sql += " WHERE m.available=1";
    sql += " ORDER BY c.sort,m.id";
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        MenuItem mi;
        mi.id = sqlite3_column_int(st, 0);
        mi.category_id = sqlite3_column_int(st, 1);
        mi.category = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        mi.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        mi.base_price = sqlite3_column_double(st, 4);
        std::string t = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
        mi.type = t == "drink" ? ItemType::Drink : (t == "combo" ? ItemType::Combo : ItemType::Food);
        mi.size_label = reinterpret_cast<const char*>(sqlite3_column_text(st, 6));
        mi.available = sqlite3_column_int(st, 7) != 0;
        mi.prep_seconds = sqlite3_column_int(st, 8);
        mi.allergens = reinterpret_cast<const char*>(sqlite3_column_text(st, 9));
        if (sqlite3_column_type(st, 10) != SQLITE_NULL) mi.happy_hour_price = sqlite3_column_double(st, 10);
        mi.is_combo = sqlite3_column_int(st, 11) != 0;
        load_recipe_into(mi);
        out.push_back(std::move(mi));
    }
    sqlite3_finalize(st);
    return out;
}

MenuItem Ledger::menu_item(int id) {
    auto all = menu_items(false);
    for (auto& m : all) if (m.id == id) return m;
    throw std::runtime_error("menu item not found");
}

std::vector<Modifier> Ledger::modifiers_for_item(int menu_item_id) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Modifier> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT m.id,m.name,m.price_delta,COALESCE(m.ingredient_id,0),m.ingredient_qty "
        "FROM modifiers m JOIN menu_item_modifiers mm ON mm.modifier_id=m.id WHERE mm.menu_item_id=?",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, menu_item_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Modifier m;
        m.id = sqlite3_column_int(st, 0);
        m.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        m.price_delta = sqlite3_column_double(st, 2);
        m.ingredient_id = sqlite3_column_int(st, 3);
        m.ingredient_qty = sqlite3_column_double(st, 4);
        out.push_back(m);
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<Ingredient> Ledger::ingredients() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Ingredient> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,sku,name,unit,stock_qty,reorder_level,cost_per_unit,allergen_flags FROM ingredients ORDER BY name",
        -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Ingredient i;
        i.id = sqlite3_column_int(st, 0);
        i.sku = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        i.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        i.unit = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        i.stock_qty = sqlite3_column_double(st, 4);
        i.reorder_level = sqlite3_column_double(st, 5);
        i.cost_per_unit = sqlite3_column_double(st, 6);
        i.allergen_flags = reinterpret_cast<const char*>(sqlite3_column_text(st, 7));
        out.push_back(i);
    }
    sqlite3_finalize(st);
    return out;
}

Ingredient Ledger::ingredient(int id) {
    for (auto& i : ingredients()) if (i.id == id) return i;
    throw std::runtime_error("ingredient not found");
}

void Ledger::apply_ledger(int ingredient_id, double delta, LedgerReason reason,
                          const std::string& ref_type, int ref_id,
                          const std::string& note, int staff_id) {
    const std::string ts = now_iso();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO inventory_ledger(ts,ingredient_id,delta,reason,ref_type,ref_id,note,staff_id) VALUES(?,?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, ingredient_id);
    sqlite3_bind_double(st, 3, delta);
    sqlite3_bind_text(st, 4, to_string(reason), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, ref_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, ref_id);
    sqlite3_bind_text(st, 7, note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, staff_id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    sqlite3_stmt* u = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE ingredients SET stock_qty = stock_qty + ? WHERE id=?", -1, &u, nullptr);
    sqlite3_bind_double(u, 1, delta);
    sqlite3_bind_int(u, 2, ingredient_id);
    sqlite3_step(u);
    sqlite3_finalize(u);

    sqlite3_stmt* b = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO inventory_balances(ingredient_id,qty,updated_at) "
        "SELECT id,stock_qty,? FROM ingredients WHERE id=? "
        "ON CONFLICT(ingredient_id) DO UPDATE SET qty=excluded.qty, updated_at=excluded.updated_at",
        -1, &b, nullptr);
    sqlite3_bind_text(b, 1, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(b, 2, ingredient_id);
    sqlite3_step(b);
    sqlite3_finalize(b);
}

void Ledger::receive_stock(int ingredient_id, double qty, int staff_id, const std::string& note) {
    std::lock_guard<std::mutex> lock(mu_);
    begin();
    try {
        apply_ledger(ingredient_id, std::abs(qty), LedgerReason::Receive, "manual", 0, note, staff_id);
        commit();
    } catch (...) { rollback(); throw; }
}

void Ledger::waste_stock(int ingredient_id, double qty, int staff_id, const std::string& note) {
    std::lock_guard<std::mutex> lock(mu_);
    begin();
    try {
        apply_ledger(ingredient_id, -std::abs(qty), LedgerReason::Waste, "manual", 0, note, staff_id);
        commit();
    } catch (...) { rollback(); throw; }
}

void Ledger::adjust_stock(int ingredient_id, double new_qty, int staff_id, const std::string& note) {
    std::lock_guard<std::mutex> lock(mu_);
    begin();
    try {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "SELECT stock_qty FROM ingredients WHERE id=?", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, ingredient_id);
        double cur = 0;
        if (sqlite3_step(st) == SQLITE_ROW) cur = sqlite3_column_double(st, 0);
        sqlite3_finalize(st);
        apply_ledger(ingredient_id, new_qty - cur, LedgerReason::Adjustment, "manual", 0, note, staff_id);
        commit();
    } catch (...) { rollback(); throw; }
}

std::vector<StockNeed> Ledger::explode_recipe(int menu_item_id, int qty) {
    std::vector<StockNeed> needs;
    // direct recipe
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT i.id,i.name,i.unit,rl.qty * ?, i.stock_qty FROM recipe_lines rl "
        "JOIN recipes r ON r.id=rl.recipe_id JOIN ingredients i ON i.id=rl.ingredient_id "
        "WHERE r.menu_item_id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, qty);
    sqlite3_bind_int(st, 2, menu_item_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        StockNeed n;
        n.ingredient_id = sqlite3_column_int(st, 0);
        n.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        n.unit = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        n.qty = sqlite3_column_double(st, 3);
        n.available = sqlite3_column_double(st, 4);
        needs.push_back(n);
    }
    sqlite3_finalize(st);

    // combo components
    sqlite3_stmt* c = nullptr;
    sqlite3_prepare_v2(db_, "SELECT component_item_id FROM combo_components WHERE combo_item_id=?", -1, &c, nullptr);
    sqlite3_bind_int(c, 1, menu_item_id);
    while (sqlite3_step(c) == SQLITE_ROW) {
        int cid = sqlite3_column_int(c, 0);
        auto sub = explode_recipe(cid, qty);
        needs.insert(needs.end(), sub.begin(), sub.end());
    }
    sqlite3_finalize(c);

    // merge same ingredients
    std::map<int, StockNeed> merged;
    for (auto& n : needs) {
        auto& m = merged[n.ingredient_id];
        m.ingredient_id = n.ingredient_id;
        m.name = n.name;
        m.unit = n.unit;
        m.qty += n.qty;
        m.available = n.available;
    }
    std::vector<StockNeed> out;
    for (auto& kv : merged) out.push_back(kv.second);
    return out;
}

bool Ledger::can_make(int menu_item_id, int qty, std::vector<StockNeed>* missing) {
    std::lock_guard<std::mutex> lock(mu_);
    auto needs = explode_recipe(menu_item_id, qty);
    bool ok = true;
    for (auto& n : needs) {
        if (n.available + 1e-9 < n.qty) {
            ok = false;
            if (missing) missing->push_back(n);
        }
    }
    return ok;
}

Ledger::PlaceOrderResult Ledger::place_order(int session_id, const std::vector<CartLine>& cart,
                                             int staff_id, int priority, const std::string& note) {
    std::lock_guard<std::mutex> lock(mu_);
    PlaceOrderResult res;
    if (cart.empty()) { res.error = "Cart is empty"; return res; }

    begin();
    try {
        const bool hh = is_happy_hour();
        // gather all stock needs including modifiers
        std::map<int, StockNeed> total_need;
        for (const auto& cl : cart) {
            auto needs = explode_recipe(cl.menu_item_id, cl.qty);
            for (auto& n : needs) {
                auto& m = total_need[n.ingredient_id];
                m.ingredient_id = n.ingredient_id;
                m.name = n.name;
                m.unit = n.unit;
                m.qty += n.qty;
                m.available = n.available;
            }
            for (int mid : cl.modifier_ids) {
                sqlite3_stmt* ms = nullptr;
                sqlite3_prepare_v2(db_,
                    "SELECT COALESCE(ingredient_id,0), ingredient_qty, name FROM modifiers WHERE id=?",
                    -1, &ms, nullptr);
                sqlite3_bind_int(ms, 1, mid);
                if (sqlite3_step(ms) == SQLITE_ROW) {
                    int iid = sqlite3_column_int(ms, 0);
                    double q = sqlite3_column_double(ms, 1) * cl.qty;
                    if (iid > 0 && q > 0) {
                        sqlite3_stmt* ig = nullptr;
                        sqlite3_prepare_v2(db_, "SELECT name,unit,stock_qty FROM ingredients WHERE id=?", -1, &ig, nullptr);
                        sqlite3_bind_int(ig, 1, iid);
                        if (sqlite3_step(ig) == SQLITE_ROW) {
                            auto& m = total_need[iid];
                            m.ingredient_id = iid;
                            m.name = reinterpret_cast<const char*>(sqlite3_column_text(ig, 0));
                            m.unit = reinterpret_cast<const char*>(sqlite3_column_text(ig, 1));
                            m.available = sqlite3_column_double(ig, 2);
                            m.qty += q;
                        }
                        sqlite3_finalize(ig);
                    }
                }
                sqlite3_finalize(ms);
            }
        }

        for (auto& kv : total_need) {
            if (kv.second.available + 1e-9 < kv.second.qty) {
                res.missing.push_back(kv.second);
            }
        }
        if (!res.missing.empty()) {
            rollback();
            res.error = "Insufficient stock for one or more ingredients";
            return res;
        }

        const std::string ts = now_iso();
        sqlite3_stmt* o = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT INTO orders(session_id,placed_at,status,priority,placed_by,note) VALUES(?,?,'placed',?,?,?)",
            -1, &o, nullptr);
        sqlite3_bind_int(o, 1, session_id);
        sqlite3_bind_text(o, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(o, 3, priority);
        sqlite3_bind_int(o, 4, staff_id);
        sqlite3_bind_text(o, 5, note.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(o) != SQLITE_DONE) { sqlite3_finalize(o); throw std::runtime_error("insert order"); }
        sqlite3_finalize(o);
        int order_id = last_id();
        res.order_id = order_id;

        bool has_food = false, has_drink = false;
        for (const auto& cl : cart) {
            // price
            sqlite3_stmt* mi = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT base_price,happy_hour_price,item_type,name FROM menu_items WHERE id=?", -1, &mi, nullptr);
            sqlite3_bind_int(mi, 1, cl.menu_item_id);
            if (sqlite3_step(mi) != SQLITE_ROW) { sqlite3_finalize(mi); throw std::runtime_error("bad item"); }
            double price = sqlite3_column_double(mi, 0);
            if (hh && sqlite3_column_type(mi, 1) != SQLITE_NULL)
                price = sqlite3_column_double(mi, 1);
            std::string itype = reinterpret_cast<const char*>(sqlite3_column_text(mi, 2));
            sqlite3_finalize(mi);
            if (itype == "drink") has_drink = true; else has_food = true;

            double mod_sum = 0;
            for (int mid : cl.modifier_ids) {
                sqlite3_stmt* mp = nullptr;
                sqlite3_prepare_v2(db_, "SELECT price_delta FROM modifiers WHERE id=?", -1, &mp, nullptr);
                sqlite3_bind_int(mp, 1, mid);
                if (sqlite3_step(mp) == SQLITE_ROW) mod_sum += sqlite3_column_double(mp, 0);
                sqlite3_finalize(mp);
            }
            double unit = price + mod_sum;
            // cart may override unit_price if set
            if (cl.unit_price > 0) unit = cl.unit_price;

            sqlite3_stmt* l = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO order_lines(order_id,menu_item_id,qty,unit_price,status,note,seat_no) VALUES(?,?,?,?,'placed',?,?)",
                -1, &l, nullptr);
            sqlite3_bind_int(l, 1, order_id);
            sqlite3_bind_int(l, 2, cl.menu_item_id);
            sqlite3_bind_int(l, 3, cl.qty);
            sqlite3_bind_double(l, 4, unit);
            sqlite3_bind_text(l, 5, cl.note.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(l, 6, cl.seat_no);
            sqlite3_step(l);
            sqlite3_finalize(l);
            int line_id = last_id();
            for (int mid : cl.modifier_ids) {
                sqlite3_stmt* om = nullptr;
                sqlite3_prepare_v2(db_, "INSERT INTO order_line_modifiers(order_line_id,modifier_id) VALUES(?,?)", -1, &om, nullptr);
                sqlite3_bind_int(om, 1, line_id);
                sqlite3_bind_int(om, 2, mid);
                sqlite3_step(om);
                sqlite3_finalize(om);
            }
        }

        // consume stock
        for (auto& kv : total_need) {
            apply_ledger(kv.first, -kv.second.qty, LedgerReason::OrderConsume, "order", order_id, "order consume", staff_id);
        }

        // kitchen tickets
        auto make_ticket = [&](const char* station) {
            sqlite3_stmt* t = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO kitchen_tickets(order_id,station,created_at,status,priority) VALUES(?,?,?,'queued',?)",
                -1, &t, nullptr);
            sqlite3_bind_int(t, 1, order_id);
            sqlite3_bind_text(t, 2, station, -1, SQLITE_STATIC);
            sqlite3_bind_text(t, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(t, 4, priority);
            sqlite3_step(t);
            sqlite3_finalize(t);
            res.ticket_ids.push_back(last_id());
        };
        if (has_food) make_ticket("kitchen");
        if (has_drink) make_ticket("bar");

        // table status
        sqlite3_stmt* tsess = nullptr;
        sqlite3_prepare_v2(db_, "SELECT table_id FROM sessions WHERE id=?", -1, &tsess, nullptr);
        sqlite3_bind_int(tsess, 1, session_id);
        int table_id = 0;
        if (sqlite3_step(tsess) == SQLITE_ROW) table_id = sqlite3_column_int(tsess, 0);
        sqlite3_finalize(tsess);
        if (table_id) {
            sqlite3_stmt* u = nullptr;
            sqlite3_prepare_v2(db_, "UPDATE dining_tables SET status='waiting' WHERE id=?", -1, &u, nullptr);
            sqlite3_bind_int(u, 1, table_id);
            sqlite3_step(u);
            sqlite3_finalize(u);
        }

        commit();
        res.ok = true;
        return res;
    } catch (const std::exception& e) {
        rollback();
        res.error = e.what();
        return res;
    }
}

bool Ledger::void_order(int order_id, int staff_id, const std::string& reason, bool restock) {
    std::lock_guard<std::mutex> lock(mu_);
    begin();
    try {
        if (restock) {
            // rebuild consumption from ledger
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT ingredient_id, delta FROM inventory_ledger WHERE ref_type='order' AND ref_id=? AND reason='order_consume'",
                -1, &st, nullptr);
            sqlite3_bind_int(st, 1, order_id);
            while (sqlite3_step(st) == SQLITE_ROW) {
                int iid = sqlite3_column_int(st, 0);
                double delta = sqlite3_column_double(st, 1); // negative
                apply_ledger(iid, -delta, LedgerReason::VoidRestock, "order", order_id, reason, staff_id);
            }
            sqlite3_finalize(st);
        }
        exec("UPDATE orders SET status='void' WHERE id=" + std::to_string(order_id));
        exec("UPDATE order_lines SET status='void' WHERE order_id=" + std::to_string(order_id));
        exec("UPDATE kitchen_tickets SET status='cancelled' WHERE order_id=" + std::to_string(order_id));
        commit();
        return true;
    } catch (...) { rollback(); return false; }
}

bool Ledger::set_order_status(int order_id, OrderStatus st) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE orders SET status=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, to_string(st), -1, SQLITE_STATIC);
    sqlite3_bind_int(s, 2, order_id);
    bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}

bool Ledger::set_order_line_status(int line_id, OrderStatus st) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE order_lines SET status=? WHERE id=?", -1, &s, nullptr);
    sqlite3_bind_text(s, 1, to_string(st), -1, SQLITE_STATIC);
    sqlite3_bind_int(s, 2, line_id);
    bool ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}

std::vector<KitchenTicket> Ledger::load_open_tickets() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<KitchenTicket> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT kt.id,kt.order_id,kt.station,kt.created_at,kt.status,kt.priority,"
        " s.table_id, t.label, o.note "
        "FROM kitchen_tickets kt "
        "JOIN orders o ON o.id=kt.order_id "
        "JOIN sessions s ON s.id=o.session_id "
        "JOIN dining_tables t ON t.id=s.table_id "
        "WHERE kt.status IN ('queued','in_progress') ORDER BY kt.priority DESC, kt.id",
        -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        KitchenTicket kt;
        kt.id = sqlite3_column_int(st, 0);
        kt.order_id = sqlite3_column_int(st, 1);
        std::string station = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        kt.station = station == "bar" ? TicketStation::Bar : TicketStation::Kitchen;
        kt.created_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        std::string status = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        if (status == "in_progress") kt.status = TicketStatus::InProgress;
        else if (status == "done") kt.status = TicketStatus::Done;
        else if (status == "cancelled") kt.status = TicketStatus::Cancelled;
        else kt.status = TicketStatus::Queued;
        kt.priority = sqlite3_column_int(st, 5);
        kt.table_id = sqlite3_column_int(st, 6);
        kt.table_label = reinterpret_cast<const char*>(sqlite3_column_text(st, 7));
        kt.note = reinterpret_cast<const char*>(sqlite3_column_text(st, 8));

        sqlite3_stmt* l = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT ol.id,ol.menu_item_id,m.name,ol.qty,ol.unit_price,ol.status,ol.note,ol.seat_no,m.item_type "
            "FROM order_lines ol JOIN menu_items m ON m.id=ol.menu_item_id WHERE ol.order_id=? AND ol.status!='void'",
            -1, &l, nullptr);
        sqlite3_bind_int(l, 1, kt.order_id);
        while (sqlite3_step(l) == SQLITE_ROW) {
            std::string itype = reinterpret_cast<const char*>(sqlite3_column_text(l, 8));
            bool drink = itype == "drink";
            if ((kt.station == TicketStation::Bar && !drink) ||
                (kt.station == TicketStation::Kitchen && drink)) continue;
            OrderLine line;
            line.id = sqlite3_column_int(l, 0);
            line.menu_item_id = sqlite3_column_int(l, 1);
            line.name = reinterpret_cast<const char*>(sqlite3_column_text(l, 2));
            line.qty = sqlite3_column_int(l, 3);
            line.unit_price = sqlite3_column_double(l, 4);
            line.status = order_status_from(reinterpret_cast<const char*>(sqlite3_column_text(l, 5)));
            line.note = reinterpret_cast<const char*>(sqlite3_column_text(l, 6));
            line.seat_no = sqlite3_column_int(l, 7);
            kt.lines.push_back(line);
        }
        sqlite3_finalize(l);
        out.push_back(std::move(kt));
    }
    sqlite3_finalize(st);
    return out;
}

bool Ledger::set_ticket_status(int ticket_id, TicketStatus st) {
    std::lock_guard<std::mutex> lock(mu_);
    const char* s = "queued";
    if (st == TicketStatus::InProgress) s = "in_progress";
    else if (st == TicketStatus::Done) s = "done";
    else if (st == TicketStatus::Cancelled) s = "cancelled";
    sqlite3_stmt* q = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE kitchen_tickets SET status=? WHERE id=?", -1, &q, nullptr);
    sqlite3_bind_text(q, 1, s, -1, SQLITE_STATIC);
    sqlite3_bind_int(q, 2, ticket_id);
    bool ok = sqlite3_step(q) == SQLITE_DONE;
    sqlite3_finalize(q);

    if (st == TicketStatus::Done || st == TicketStatus::InProgress) {
        // sync order status
        sqlite3_stmt* o = nullptr;
        sqlite3_prepare_v2(db_, "SELECT order_id FROM kitchen_tickets WHERE id=?", -1, &o, nullptr);
        sqlite3_bind_int(o, 1, ticket_id);
        int oid = 0;
        if (sqlite3_step(o) == SQLITE_ROW) oid = sqlite3_column_int(o, 0);
        sqlite3_finalize(o);
        if (oid) {
            const char* os = st == TicketStatus::Done ? "ready" : "preparing";
            sqlite3_stmt* u = nullptr;
            sqlite3_prepare_v2(db_, "UPDATE orders SET status=? WHERE id=?", -1, &u, nullptr);
            sqlite3_bind_text(u, 1, os, -1, SQLITE_STATIC);
            sqlite3_bind_int(u, 2, oid);
            sqlite3_step(u);
            sqlite3_finalize(u);
            if (st == TicketStatus::Done) {
                sqlite3_stmt* ts = nullptr;
                sqlite3_prepare_v2(db_,
                    "UPDATE dining_tables SET status='bill' WHERE id=("
                    " SELECT s.table_id FROM sessions s JOIN orders o ON o.session_id=s.id WHERE o.id=?)",
                    -1, &ts, nullptr);
                // keep waiting until pay; only mark ready on order - use occupied if was waiting
                // actually set to occupied with ready food - use 'bill' only when requested
                sqlite3_finalize(ts);
            }
        }
    }
    return ok;
}

std::string Ledger::next_receipt_no() {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM receipts", -1, &st, nullptr);
    int n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "RP-%06d", n + 1);
    return buf;
}

Ledger::PayResult Ledger::close_and_pay(const PayInput& in) {
    std::lock_guard<std::mutex> lock(mu_);
    PayResult res;
    begin();
    try {
        // sum open session lines
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT ol.qty, ol.unit_price, m.name FROM order_lines ol "
            "JOIN orders o ON o.id=ol.order_id JOIN menu_items m ON m.id=ol.menu_item_id "
            "WHERE o.session_id=? AND o.status!='void' AND ol.status!='void'",
            -1, &st, nullptr);
        sqlite3_bind_int(st, 1, in.session_id);
        double subtotal = 0;
        struct L { std::string d; int q; double p; };
        std::vector<L> lines;
        while (sqlite3_step(st) == SQLITE_ROW) {
            L l;
            l.q = sqlite3_column_int(st, 0);
            l.p = sqlite3_column_double(st, 1);
            l.d = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
            subtotal += l.q * l.p;
            lines.push_back(l);
        }
        sqlite3_finalize(st);
        if (lines.empty()) { rollback(); res.error = "Nothing to pay"; return res; }

        double discount = std::max(0.0, in.discount);
        double taxable = std::max(0.0, subtotal - discount);
        double service = round2(taxable * in.service_charge_rate);
        double tax = round2((taxable + service) * in.tax_rate);
        double tip = std::max(0.0, in.tip);
        double total = round2(taxable + service + tax + tip);

        double paid = 0;
        for (auto& p : in.payments) paid += p.amount;
        if (paid + 1e-6 < total) { rollback(); res.error = "Payment short"; return res; }

        const std::string ts = now_iso();
        std::string rno = next_receipt_no();
        sqlite3_stmt* r = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT INTO receipts(session_id,issued_at,subtotal,tax,service_charge,discount,tip,total,status,receipt_no,cashier_id,print_count,customer_name) "
            "VALUES(?,?,?,?,?,?,?,?,'issued',?,?,1,?)", -1, &r, nullptr);
        sqlite3_bind_int(r, 1, in.session_id);
        sqlite3_bind_text(r, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(r, 3, subtotal);
        sqlite3_bind_double(r, 4, tax);
        sqlite3_bind_double(r, 5, service);
        sqlite3_bind_double(r, 6, discount);
        sqlite3_bind_double(r, 7, tip);
        sqlite3_bind_double(r, 8, total);
        sqlite3_bind_text(r, 9, rno.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(r, 10, in.cashier_id);
        sqlite3_bind_text(r, 11, in.customer_name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(r) != SQLITE_DONE) { sqlite3_finalize(r); throw std::runtime_error("receipt insert"); }
        sqlite3_finalize(r);
        int rid = last_id();

        for (auto& l : lines) {
            sqlite3_stmt* rl = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO receipt_lines(receipt_id,description,qty,unit_price,line_total) VALUES(?,?,?,?,?)",
                -1, &rl, nullptr);
            sqlite3_bind_int(rl, 1, rid);
            sqlite3_bind_text(rl, 2, l.d.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(rl, 3, l.q);
            sqlite3_bind_double(rl, 4, l.p);
            sqlite3_bind_double(rl, 5, l.q * l.p);
            sqlite3_step(rl);
            sqlite3_finalize(rl);
        }
        for (auto& p : in.payments) {
            sqlite3_stmt* rp = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO receipt_payments(receipt_id,method,amount) VALUES(?,?,?)", -1, &rp, nullptr);
            sqlite3_bind_int(rp, 1, rid);
            sqlite3_bind_text(rp, 2, to_string(p.method), -1, SQLITE_STATIC);
            sqlite3_bind_double(rp, 3, p.amount);
            sqlite3_step(rp);
            sqlite3_finalize(rp);
        }

        // close session + table dirty
        sqlite3_stmt* cs = nullptr;
        sqlite3_prepare_v2(db_, "UPDATE sessions SET status='closed', closed_at=? WHERE id=?", -1, &cs, nullptr);
        sqlite3_bind_text(cs, 1, ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(cs, 2, in.session_id);
        sqlite3_step(cs);
        sqlite3_finalize(cs);

        sqlite3_stmt* ut = nullptr;
        sqlite3_prepare_v2(db_,
            "UPDATE dining_tables SET status='dirty' WHERE id=(SELECT table_id FROM sessions WHERE id=?)",
            -1, &ut, nullptr);
        sqlite3_bind_int(ut, 1, in.session_id);
        sqlite3_step(ut);
        sqlite3_finalize(ut);

        // mark orders served
        sqlite3_stmt* uo = nullptr;
        sqlite3_prepare_v2(db_, "UPDATE orders SET status='served' WHERE session_id=? AND status!='void'", -1, &uo, nullptr);
        sqlite3_bind_int(uo, 1, in.session_id);
        sqlite3_step(uo);
        sqlite3_finalize(uo);

        // loyalty
        if (!in.customer_name.empty()) {
            int pts = static_cast<int>(total);
            sqlite3_stmt* ly = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO loyalty(customer_name,stamps,points,updated_at) VALUES(?,1,?,?) "
                "ON CONFLICT(customer_name) DO UPDATE SET stamps=stamps+1, points=points+excluded.points, updated_at=excluded.updated_at",
                -1, &ly, nullptr);
            sqlite3_bind_text(ly, 1, in.customer_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ly, 2, pts);
            sqlite3_bind_text(ly, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ly);
            sqlite3_finalize(ly);
        }

        commit();
        res.ok = true;
        res.receipt.id = rid;
        res.receipt.session_id = in.session_id;
        res.receipt.issued_at = ts;
        res.receipt.subtotal = subtotal;
        res.receipt.tax = tax;
        res.receipt.service_charge = service;
        res.receipt.discount = discount;
        res.receipt.tip = tip;
        res.receipt.total = total;
        res.receipt.receipt_no = rno;
        res.receipt.cashier_id = in.cashier_id;
        res.receipt.customer_name = in.customer_name;
        res.receipt.payments = in.payments;
        for (auto& l : lines)
            res.receipt.lines.push_back({l.d, {l.q, l.p}});
        return res;
    } catch (const std::exception& e) {
        rollback();
        res.error = e.what();
        return res;
    }
}

std::optional<Receipt> Ledger::receipt(int id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,session_id,issued_at,subtotal,tax,service_charge,discount,tip,total,status,receipt_no,COALESCE(cashier_id,0),print_count,customer_name "
        "FROM receipts WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, id);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return std::nullopt; }
    Receipt r;
    r.id = sqlite3_column_int(st, 0);
    r.session_id = sqlite3_column_int(st, 1);
    r.issued_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    r.subtotal = sqlite3_column_double(st, 3);
    r.tax = sqlite3_column_double(st, 4);
    r.service_charge = sqlite3_column_double(st, 5);
    r.discount = sqlite3_column_double(st, 6);
    r.tip = sqlite3_column_double(st, 7);
    r.total = sqlite3_column_double(st, 8);
    std::string stt = reinterpret_cast<const char*>(sqlite3_column_text(st, 9));
    r.status = stt == "void" ? ReceiptStatus::Void : (stt == "refunded" ? ReceiptStatus::Refunded : ReceiptStatus::Issued);
    r.receipt_no = reinterpret_cast<const char*>(sqlite3_column_text(st, 10));
    r.cashier_id = sqlite3_column_int(st, 11);
    r.print_count = sqlite3_column_int(st, 12);
    r.customer_name = reinterpret_cast<const char*>(sqlite3_column_text(st, 13));
    sqlite3_finalize(st);

    sqlite3_stmt* l = nullptr;
    sqlite3_prepare_v2(db_, "SELECT description,qty,unit_price FROM receipt_lines WHERE receipt_id=?", -1, &l, nullptr);
    sqlite3_bind_int(l, 1, id);
    while (sqlite3_step(l) == SQLITE_ROW) {
        std::string d = reinterpret_cast<const char*>(sqlite3_column_text(l, 0));
        int q = sqlite3_column_int(l, 1);
        double p = sqlite3_column_double(l, 2);
        r.lines.push_back({d, {q, p}});
    }
    sqlite3_finalize(l);

    sqlite3_stmt* p = nullptr;
    sqlite3_prepare_v2(db_, "SELECT method,amount FROM receipt_payments WHERE receipt_id=?", -1, &p, nullptr);
    sqlite3_bind_int(p, 1, id);
    while (sqlite3_step(p) == SQLITE_ROW) {
        ReceiptPayment rp;
        std::string m = reinterpret_cast<const char*>(sqlite3_column_text(p, 0));
        if (m == "card") rp.method = PayMethod::Card;
        else if (m == "mixed") rp.method = PayMethod::Mixed;
        else if (m == "other") rp.method = PayMethod::Other;
        else rp.method = PayMethod::Cash;
        rp.amount = sqlite3_column_double(p, 1);
        r.payments.push_back(rp);
    }
    sqlite3_finalize(p);
    return r;
}

std::vector<Receipt> Ledger::recent_receipts(int limit) {
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lock(mu_);
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT id FROM receipts ORDER BY id DESC LIMIT ?", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, limit);
        while (sqlite3_step(st) == SQLITE_ROW) ids.push_back(sqlite3_column_int(st, 0));
        sqlite3_finalize(st);
    }
    std::vector<Receipt> out;
    for (int id : ids) {
        auto r = receipt(id);
        if (r) out.push_back(*r);
    }
    return out;
}

bool Ledger::void_receipt(int receipt_id, int manager_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mu_);
    (void)manager_id;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE receipts SET status='void' WHERE id=? AND status='issued'", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, receipt_id);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    if (ok) {
        // note only — physical stock already consumed; manager may restock manually
        (void)reason;
    }
    return ok;
}

void Ledger::bump_print_count(int receipt_id) {
    std::lock_guard<std::mutex> lock(mu_);
    exec("UPDATE receipts SET print_count=print_count+1 WHERE id=" + std::to_string(receipt_id));
}

int Ledger::add_waitlist(const std::string& name, int party, const std::string& phone, int eta) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string ts = now_iso();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO waitlist(name,party_size,phone,created_at,status,estimated_wait_min) VALUES(?,?,?,?,'waiting',?)",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, party);
    sqlite3_bind_text(st, 3, phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, eta);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return last_id();
}

std::vector<WaitlistEntry> Ledger::waitlist() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<WaitlistEntry> out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,name,party_size,phone,created_at,status,estimated_wait_min FROM waitlist "
        "WHERE status='waiting' ORDER BY id", -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        WaitlistEntry w;
        w.id = sqlite3_column_int(st, 0);
        w.name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        w.party_size = sqlite3_column_int(st, 2);
        w.phone = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        w.created_at = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        w.status = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
        w.estimated_wait_min = sqlite3_column_int(st, 6);
        out.push_back(w);
    }
    sqlite3_finalize(st);
    return out;
}

void Ledger::set_waitlist_status(int id, const std::string& status) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE waitlist SET status=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

Loyalty Ledger::loyalty_get(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    Loyalty l;
    l.customer_name = name;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT stamps,points FROM loyalty WHERE customer_name=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        l.stamps = sqlite3_column_int(st, 0);
        l.points = sqlite3_column_int(st, 1);
    }
    sqlite3_finalize(st);
    return l;
}

void Ledger::loyalty_add_stamps(const std::string& name, int stamps, int points) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string ts = now_iso();
    sqlite3_stmt* ly = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO loyalty(customer_name,stamps,points,updated_at) VALUES(?,?,?,?) "
        "ON CONFLICT(customer_name) DO UPDATE SET stamps=stamps+excluded.stamps, points=points+excluded.points, updated_at=excluded.updated_at",
        -1, &ly, nullptr);
    sqlite3_bind_text(ly, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ly, 2, stamps);
    sqlite3_bind_int(ly, 3, points);
    sqlite3_bind_text(ly, 4, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(ly);
    sqlite3_finalize(ly);
}

DayReport Ledger::day_report(const std::string& day) {
    std::lock_guard<std::mutex> lock(mu_);
    DayReport r;
    r.day = day;
    std::string like = day + "%";
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT COUNT(*), COALESCE(SUM(total),0), COALESCE(SUM(tax),0), COALESCE(SUM(tip),0) "
        "FROM receipts WHERE issued_at LIKE ? AND status='issued'", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        r.receipts = sqlite3_column_int(st, 0);
        r.revenue = sqlite3_column_double(st, 1);
        r.tax = sqlite3_column_double(st, 2);
        r.tips = sqlite3_column_double(st, 3);
    }
    sqlite3_finalize(st);

    sqlite3_stmt* c = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT COALESCE(SUM(s.covers),0) FROM sessions s WHERE s.closed_at LIKE ? AND s.status='closed'",
        -1, &c, nullptr);
    sqlite3_bind_text(c, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(c) == SQLITE_ROW) r.covers = sqlite3_column_int(c, 0);
    sqlite3_finalize(c);

    sqlite3_stmt* v = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM receipts WHERE issued_at LIKE ? AND status='void'", -1, &v, nullptr);
    sqlite3_bind_text(v, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(v) == SQLITE_ROW) r.voids = sqlite3_column_int(v, 0);
    sqlite3_finalize(v);

    sqlite3_stmt* top = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT rl.description, SUM(rl.qty) as q FROM receipt_lines rl "
        "JOIN receipts r ON r.id=rl.receipt_id WHERE r.issued_at LIKE ? AND r.status='issued' "
        "GROUP BY rl.description ORDER BY q DESC LIMIT 10", -1, &top, nullptr);
    sqlite3_bind_text(top, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(top) == SQLITE_ROW)
        r.top_sellers.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(top, 0)), sqlite3_column_int(top, 1));
    sqlite3_finalize(top);

    sqlite3_stmt* us = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT i.name, SUM(-l.delta) FROM inventory_ledger l JOIN ingredients i ON i.id=l.ingredient_id "
        "WHERE l.ts LIKE ? AND l.reason='order_consume' GROUP BY i.name ORDER BY 2 DESC LIMIT 15",
        -1, &us, nullptr);
    sqlite3_bind_text(us, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(us) == SQLITE_ROW)
        r.ingredient_usage.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(us, 0)), sqlite3_column_double(us, 1));
    sqlite3_finalize(us);
    return r;
}

void Ledger::ensure_role_policy() {
    std::lock_guard<std::mutex> lock(mu_);
    // Only manager + waiter may log in for this POS mode.
    exec("UPDATE staff SET active=0 WHERE role NOT IN ('manager','waiter')");

    // Guarantee at least one manager and one waiter exist (re-seed pins if wiped).
    auto count_role = [&](const char* role) {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM staff WHERE role=? AND active=1", -1, &st, nullptr);
        sqlite3_bind_text(st, 1, role, -1, SQLITE_STATIC);
        int n = 0;
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return n;
    };
    auto pin_pair = [](const std::string& p) {
        std::string salt = "rp_salt_v1";
        return std::make_pair(salt, sha256::hash_pin(p, salt));
    };
    auto insert_staff = [&](const char* name, const char* role, const char* pin) {
        auto [salt, hash] = pin_pair(pin);
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT INTO staff(name,role,pin_hash,pin_salt,active) VALUES(?,?,?,?,1)", -1, &st, nullptr);
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, role, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    };
    if (count_role("manager") == 0) insert_staff("Ayse Yilmaz", "manager", "0000");
    if (count_role("waiter") == 0) {
        insert_staff("Mehmet Kaya", "waiter", "1111");
        insert_staff("Zeynep Arslan", "waiter", "2222");
    }
}

Ledger::NewProductResult Ledger::add_menu_product(const NewProductInput& in) {
    std::lock_guard<std::mutex> lock(mu_);
    NewProductResult res;
    if (in.name.empty()) { res.error = "Name required"; return res; }
    if (in.base_price < 0) { res.error = "Invalid price"; return res; }
    if (in.category_id <= 0) { res.error = "Category required"; return res; }
    if (in.item_type != "food" && in.item_type != "drink" && in.item_type != "combo") {
        res.error = "Type must be food, drink, or combo";
        return res;
    }

    begin();
    try {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT INTO menu_items(category_id,name,base_price,item_type,size_label,available,prep_seconds,allergens,happy_hour_price,is_combo) "
            "VALUES(?,?,?,?,?,1,?,?,?,?)", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, in.category_id);
        sqlite3_bind_text(st, 2, in.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(st, 3, in.base_price);
        sqlite3_bind_text(st, 4, in.item_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, in.size_label.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 6, in.prep_seconds);
        sqlite3_bind_text(st, 7, in.allergens.c_str(), -1, SQLITE_TRANSIENT);
        if (in.happy_hour_price) sqlite3_bind_double(st, 8, *in.happy_hour_price);
        else sqlite3_bind_null(st, 8);
        sqlite3_bind_int(st, 9, in.item_type == "combo" ? 1 : 0);
        if (sqlite3_step(st) != SQLITE_DONE) {
            sqlite3_finalize(st);
            throw std::runtime_error("insert menu item failed");
        }
        sqlite3_finalize(st);
        int mid = last_id();

        std::vector<std::pair<int, double>> lines = in.recipe_lines;
        if (lines.empty() && in.recipe_ingredient_id > 0 && in.recipe_qty > 0)
            lines.emplace_back(in.recipe_ingredient_id, in.recipe_qty);

        if (!lines.empty()) {
            sqlite3_stmt* r = nullptr;
            sqlite3_prepare_v2(db_, "INSERT INTO recipes(menu_item_id,name) VALUES(?,?)", -1, &r, nullptr);
            sqlite3_bind_int(r, 1, mid);
            sqlite3_bind_text(r, 2, in.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(r);
            sqlite3_finalize(r);
            int rid = last_id();
            for (auto& ln : lines) {
                if (ln.first <= 0 || ln.second <= 0) continue;
                sqlite3_stmt* rl = nullptr;
                sqlite3_prepare_v2(db_,
                    "INSERT INTO recipe_lines(recipe_id,ingredient_id,qty) VALUES(?,?,?)", -1, &rl, nullptr);
                sqlite3_bind_int(rl, 1, rid);
                sqlite3_bind_int(rl, 2, ln.first);
                sqlite3_bind_double(rl, 3, ln.second);
                sqlite3_step(rl);
                sqlite3_finalize(rl);
            }
        }

        commit();
        res.ok = true;
        res.menu_item_id = mid;
        return res;
    } catch (const std::exception& e) {
        rollback();
        res.error = e.what();
        return res;
    }
}

bool Ledger::set_menu_item_available(int menu_item_id, bool available) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE menu_items SET available=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, available ? 1 : 0);
    sqlite3_bind_int(st, 2, menu_item_id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

// ---------------- SEED ----------------
void Ledger::seed_full_mock() {
    std::lock_guard<std::mutex> lock(mu_);
    if (is_seeded()) return;
    begin();
    try {
        auto pin = [&](const std::string& p) {
            std::string salt = "rp_salt_v1";
            return std::make_pair(salt, sha256::hash_pin(p, salt));
        };
        // Only manager + waiter roles for this product mode
        struct S { const char* name; const char* role; const char* pin; };
        S staff[] = {
            {"Ayse Yilmaz", "manager", "0000"},
            {"Mehmet Kaya", "waiter", "1111"},
            {"Zeynep Arslan", "waiter", "2222"},
        };
        for (auto& s : staff) {
            auto [salt, hash] = pin(s.pin);
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO staff(name,role,pin_hash,pin_salt,active) VALUES(?,?,?,?,1)", -1, &st, nullptr);
            sqlite3_bind_text(st, 1, s.name, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, s.role, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, salt.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        // 10 tables in a floor grid
        struct T { const char* label; int seats; float x,y; };
        T tables[] = {
            {"T1", 2, 40, 40}, {"T2", 2, 160, 40}, {"T3", 4, 280, 40}, {"T4", 4, 400, 40}, {"T5", 4, 520, 40},
            {"T6", 6, 40, 180}, {"T7", 6, 200, 180}, {"T8", 4, 360, 180}, {"T9", 8, 500, 180}, {"T10", 2, 640, 180},
        };
        for (int i = 0; i < 10; ++i) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO dining_tables(id,label,seats,pos_x,pos_y,status) VALUES(?,?,?,?,?,'free')",
                -1, &st, nullptr);
            sqlite3_bind_int(st, 1, i + 1);
            sqlite3_bind_text(st, 2, tables[i].label, -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 3, tables[i].seats);
            sqlite3_bind_double(st, 4, tables[i].x);
            sqlite3_bind_double(st, 5, tables[i].y);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        // ingredients
        struct Ing { const char* sku; const char* name; const char* unit; double qty; double reorder; double cost; const char* allg; };
        Ing ings[] = {
            {"COFFEE_BEAN","Coffee beans","g",5000,800,0.03,""},
            {"ESPRESSO_BLEND","Espresso blend","g",4000,600,0.04,""},
            {"MILK","Whole milk","ml",20000,3000,0.002,"dairy"},
            {"OAT_MILK","Oat milk","ml",5000,1000,0.003,"gluten"},
            {"SUGAR","Sugar","g",3000,500,0.001,""},
            {"TEA_LEAF","Black tea leaves","g",2000,300,0.02,""},
            {"LEMON","Lemon","pcs",80,15,0.4,""},
            {"ICE","Ice cubes","g",15000,2000,0.0001,""},
            {"SMOOTHIE_MIX","Smoothie fruit mix","g",4000,600,0.01,""},
            {"BUBBLE_PEARL","Tapioca pearls","g",2500,400,0.008,""},
            {"FRAPPE_BASE","Frappe base powder","g",2000,300,0.015,"dairy"},
            {"FLOUR","Flour","g",8000,1000,0.001,"gluten"},
            {"BUTTER","Butter","g",3000,400,0.012,"dairy"},
            {"CHEESE","Cheese","g",2500,400,0.02,"dairy"},
            {"HAM","Ham","g",2000,300,0.03,""},
            {"BREAD","Bread loaf","pcs",40,8,1.2,"gluten"},
            {"EGG","Eggs","pcs",120,24,0.3,""},
            {"CHOCOLATE","Chocolate","g",2000,300,0.025,""},
            {"CREAM_CHEESE","Cream cheese","g",2000,300,0.018,"dairy"},
            {"PASTRY_SHELL","Pastry shell","pcs",60,12,0.5,"gluten,dairy"},
            {"DONUT_DOUGH","Donut dough","pcs",50,10,0.4,"gluten"},
            {"CROISSANT_DOUGH","Croissant dough","pcs",40,8,0.6,"gluten,dairy"},
            {"CUP_S","Cup small","pcs",200,40,0.05,""},
            {"CUP_M","Cup medium","pcs",200,40,0.07,""},
            {"CUP_L","Cup large","pcs",200,40,0.09,""},
            {"NAPKIN","Napkin","pcs",500,100,0.01,""},
            {"WATER","Water","ml",100000,0,0,""},
            {"SYRUP_VANILLA","Vanilla syrup","ml",2000,300,0.01,""},
            {"COCOA","Cocoa powder","g",1500,200,0.02,""},
            {"YOGURT","Yogurt","g",2000,300,0.008,"dairy"},
        };
        for (auto& i : ings) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO ingredients(sku,name,unit,stock_qty,reorder_level,cost_per_unit,allergen_flags) VALUES(?,?,?,?,?,?,?)",
                -1, &st, nullptr);
            sqlite3_bind_text(st, 1, i.sku, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, i.name, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, i.unit, -1, SQLITE_STATIC);
            sqlite3_bind_double(st, 4, i.qty);
            sqlite3_bind_double(st, 5, i.reorder);
            sqlite3_bind_double(st, 6, i.cost);
            sqlite3_bind_text(st, 7, i.allg, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
            int iid = last_id();
            apply_ledger(iid, i.qty, LedgerReason::Seed, "seed", 0, "initial stock", 1);
            // seed applied as +qty but stock already set — fix double count
            sqlite3_stmt* fix = nullptr;
            sqlite3_prepare_v2(db_, "UPDATE ingredients SET stock_qty=? WHERE id=?", -1, &fix, nullptr);
            sqlite3_bind_double(fix, 1, i.qty);
            sqlite3_bind_int(fix, 2, iid);
            sqlite3_step(fix);
            sqlite3_finalize(fix);
        }

        auto cat = [&](const char* name, int sort) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_, "INSERT INTO menu_categories(name,sort) VALUES(?,?)", -1, &st, nullptr);
            sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 2, sort);
            sqlite3_step(st);
            sqlite3_finalize(st);
            return last_id();
        };
        int cHot = cat("Hot Drinks", 1);
        int cCold = cat("Cold Drinks", 2);
        int cSweet = cat("Desserts", 3);
        int cSavory = cat("Savory", 4);
        int cCombo = cat("Combos", 5);

        auto ing_id = [&](const char* sku) -> int {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_, "SELECT id FROM ingredients WHERE sku=?", -1, &st, nullptr);
            sqlite3_bind_text(st, 1, sku, -1, SQLITE_STATIC);
            int id = 0;
            if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
            return id;
        };

        auto add_item = [&](int cat_id, const char* name, double price, const char* type,
                            const char* size, int prep, const char* allg, std::optional<double> hh) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO menu_items(category_id,name,base_price,item_type,size_label,available,prep_seconds,allergens,happy_hour_price,is_combo) "
                "VALUES(?,?,?,?,?,1,?,?,?,0)", -1, &st, nullptr);
            sqlite3_bind_int(st, 1, cat_id);
            sqlite3_bind_text(st, 2, name, -1, SQLITE_STATIC);
            sqlite3_bind_double(st, 3, price);
            sqlite3_bind_text(st, 4, type, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 5, size, -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 6, prep);
            sqlite3_bind_text(st, 7, allg, -1, SQLITE_STATIC);
            if (hh) sqlite3_bind_double(st, 8, *hh); else sqlite3_bind_null(st, 8);
            sqlite3_step(st);
            sqlite3_finalize(st);
            return last_id();
        };

        auto add_recipe = [&](int menu_id, const char* rname, std::vector<std::pair<const char*, double>> lines) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_, "INSERT INTO recipes(menu_item_id,name) VALUES(?,?)", -1, &st, nullptr);
            sqlite3_bind_int(st, 1, menu_id);
            sqlite3_bind_text(st, 2, rname, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
            int rid = last_id();
            for (auto& ln : lines) {
                sqlite3_stmt* rl = nullptr;
                sqlite3_prepare_v2(db_, "INSERT INTO recipe_lines(recipe_id,ingredient_id,qty) VALUES(?,?,?)", -1, &rl, nullptr);
                sqlite3_bind_int(rl, 1, rid);
                sqlite3_bind_int(rl, 2, ing_id(ln.first));
                sqlite3_bind_double(rl, 3, ln.second);
                sqlite3_step(rl);
                sqlite3_finalize(rl);
            }
        };

        // Hot drinks with sizes
        struct Drink { const char* name; double ps,pm,pl; const char* bean; double bg,mg,lg; double milk_s,milk_m,milk_l; };
        Drink drinks[] = {
            {"Filtre Kahve", 1.0, 2.0, 3.0, "COFFEE_BEAN", 12, 18, 24, 0, 0, 0},
            {"Americano", 2.0, 3.0, 4.0, "ESPRESSO_BLEND", 14, 18, 22, 0, 0, 0},
            {"Espresso", 1.0, 2.0, 3.0, "ESPRESSO_BLEND", 9, 14, 18, 0, 0, 0},
            {"Cappuccino", 2.0, 2.5, 3.0, "ESPRESSO_BLEND", 14, 18, 22, 120, 180, 240},
            {"Macchiato", 2.5, 3.0, 3.5, "ESPRESSO_BLEND", 14, 18, 22, 40, 60, 80},
            {"Latte", 3.0, 3.5, 4.0, "ESPRESSO_BLEND", 14, 18, 22, 180, 250, 320},
        };
        for (auto& d : drinks) {
            const char* sizes[] = {"Small","Medium","Large"};
            double prices[] = {d.ps, d.pm, d.pl};
            double beans[] = {d.bg, d.mg, d.lg};
            double milks[] = {d.milk_s, d.milk_m, d.milk_l};
            const char* cups[] = {"CUP_S","CUP_M","CUP_L"};
            for (int i = 0; i < 3; ++i) {
                std::string full = std::string(d.name) + " (" + sizes[i] + ")";
                int id = add_item(cHot, full.c_str(), prices[i], "drink", sizes[i], 180, milks[i] > 0 ? "dairy" : "",
                                  prices[i] * 0.85);
                std::vector<std::pair<const char*, double>> lines = {
                    {d.bean, beans[i]}, {"WATER", milks[i] > 0 ? 30.0 : 180.0}, {cups[i], 1}, {"NAPKIN", 1}
                };
                if (milks[i] > 0) lines.push_back({"MILK", milks[i]});
                add_recipe(id, full.c_str(), lines);
            }
        }
        // Tea
        {
            int id = add_item(cHot, "Cay (Bardak)", 2.0, "drink", "Bardak", 120, "", 1.5);
            add_recipe(id, "Cay Bardak", {{"TEA_LEAF", 3}, {"WATER", 200}, {"CUP_S", 1}, {"NAPKIN", 1}});
            id = add_item(cHot, "Cay (Kupa)", 3.0, "drink", "Kupa", 120, "", 2.0);
            add_recipe(id, "Cay Kupa", {{"TEA_LEAF", 5}, {"WATER", 300}, {"CUP_M", 1}, {"NAPKIN", 1}});
            id = add_item(cHot, "Cay (Demlik)", 15.0, "drink", "Demlik", 300, "", 12.0);
            add_recipe(id, "Cay Demlik", {{"TEA_LEAF", 20}, {"WATER", 1000}, {"CUP_L", 1}, {"NAPKIN", 4}});
        }

        // Cold
        struct Cold { const char* name; double ps,pm,pl; };
        Cold colds[] = {
            {"Smoothie", 3, 4, 5},
            {"Bubble Tea", 4, 5, 6},
            {"Frappe", 4, 4, 5},
            {"Limonata", 1, 2, 3},
        };
        for (auto& d : colds) {
            const char* sizes[] = {"Small","Medium","Large"};
            double prices[] = {d.ps, d.pm, d.pl};
            double mult[] = {0.8, 1.0, 1.3};
            const char* cups[] = {"CUP_S","CUP_M","CUP_L"};
            for (int i = 0; i < 3; ++i) {
                std::string full = std::string(d.name) + " (" + sizes[i] + ")";
                const char* allg = (std::string(d.name) == "Frappe") ? "dairy" : "";
                int id = add_item(cCold, full.c_str(), prices[i], "drink", sizes[i], 240, allg, prices[i] * 0.9);
                std::vector<std::pair<const char*, double>> lines = {{"ICE", 100 * mult[i]}, {cups[i], 1}, {"NAPKIN", 1}};
                if (std::string(d.name) == "Smoothie") {
                    lines.push_back({"SMOOTHIE_MIX", 120 * mult[i]});
                    lines.push_back({"YOGURT", 40 * mult[i]});
                } else if (std::string(d.name) == "Bubble Tea") {
                    lines.push_back({"TEA_LEAF", 4 * mult[i]});
                    lines.push_back({"BUBBLE_PEARL", 40 * mult[i]});
                    lines.push_back({"MILK", 100 * mult[i]});
                    lines.push_back({"SUGAR", 10 * mult[i]});
                } else if (std::string(d.name) == "Frappe") {
                    lines.push_back({"FRAPPE_BASE", 30 * mult[i]});
                    lines.push_back({"MILK", 150 * mult[i]});
                    lines.push_back({"SUGAR", 8 * mult[i]});
                } else {
                    lines.push_back({"LEMON", 0.5 * mult[i]});
                    lines.push_back({"SUGAR", 15 * mult[i]});
                    lines.push_back({"WATER", 250 * mult[i]});
                }
                add_recipe(id, full.c_str(), lines);
            }
        }

        // Desserts
        int cheesecake = add_item(cSweet, "Cheesecake", 5.0, "food", "", 60, "dairy,gluten", std::nullopt);
        add_recipe(cheesecake, "Cheesecake", {{"CREAM_CHEESE", 80}, {"SUGAR", 20}, {"FLOUR", 30}, {"EGG", 0.5}, {"NAPKIN", 1}});
        int eclair = add_item(cSweet, "Ekler", 4.0, "food", "", 60, "dairy,gluten", std::nullopt);
        add_recipe(eclair, "Ekler", {{"PASTRY_SHELL", 1}, {"CREAM_CHEESE", 40}, {"CHOCOLATE", 20}, {"NAPKIN", 1}});
        int donut = add_item(cSweet, "Donut", 3.0, "food", "", 30, "gluten", std::nullopt);
        add_recipe(donut, "Donut", {{"DONUT_DOUGH", 1}, {"SUGAR", 10}, {"CHOCOLATE", 15}, {"NAPKIN", 1}});
        int kru = add_item(cSweet, "Kruvasan", 4.0, "food", "", 45, "gluten,dairy", std::nullopt);
        add_recipe(kru, "Kruvasan", {{"CROISSANT_DOUGH", 1}, {"BUTTER", 15}, {"NAPKIN", 1}});

        // Savory
        int sand = add_item(cSavory, "Sandvic", 2.0, "food", "", 300, "gluten,dairy", std::nullopt);
        add_recipe(sand, "Sandvic", {{"BREAD", 0.25}, {"CHEESE", 40}, {"HAM", 50}, {"BUTTER", 10}, {"NAPKIN", 2}});
        int corek = add_item(cSavory, "Corek", 2.0, "food", "", 60, "gluten", std::nullopt);
        add_recipe(corek, "Corek", {{"FLOUR", 80}, {"BUTTER", 15}, {"EGG", 0.2}, {"NAPKIN", 1}});

        // Combo: lunch set = sandvic + cay bardak
        int cayB = 0;
        {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_, "SELECT id FROM menu_items WHERE name='Cay (Bardak)'", -1, &st, nullptr);
            if (sqlite3_step(st) == SQLITE_ROW) cayB = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        int combo = add_item(cCombo, "Ogle Menusu (Sandvic + Cay)", 3.5, "combo", "", 360, "gluten,dairy", 3.0);
        {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_, "UPDATE menu_items SET is_combo=1 WHERE id=?", -1, &st, nullptr);
            sqlite3_bind_int(st, 1, combo);
            sqlite3_step(st);
            sqlite3_finalize(st);
            sqlite3_prepare_v2(db_, "INSERT INTO combo_components(combo_item_id,component_item_id) VALUES(?,?),(?,?)", -1, &st, nullptr);
            sqlite3_bind_int(st, 1, combo); sqlite3_bind_int(st, 2, sand);
            sqlite3_bind_int(st, 3, combo); sqlite3_bind_int(st, 4, cayB);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        // Modifiers
        auto add_mod = [&](const char* name, double price, const char* sku, double q) {
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db_,
                "INSERT INTO modifiers(name,price_delta,ingredient_id,ingredient_qty) VALUES(?,?,?,?)", -1, &st, nullptr);
            sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
            sqlite3_bind_double(st, 2, price);
            int iid = sku ? ing_id(sku) : 0;
            if (iid) sqlite3_bind_int(st, 3, iid); else sqlite3_bind_null(st, 3);
            sqlite3_bind_double(st, 4, q);
            sqlite3_step(st);
            sqlite3_finalize(st);
            return last_id();
        };
        int mExtraShot = add_mod("Extra shot", 0.75, "ESPRESSO_BLEND", 9);
        int mOat = add_mod("Oat milk swap", 0.50, "OAT_MILK", 180);
        int mVanilla = add_mod("Vanilla syrup", 0.40, "SYRUP_VANILLA", 20);
        int mNoSugar = add_mod("No sugar", 0.0, nullptr, 0);
        int mExtraCheese = add_mod("Extra cheese", 0.60, "CHEESE", 30);

        // attach mods to lattes/cappuccinos and sandwich
        sqlite3_stmt* allm = nullptr;
        sqlite3_prepare_v2(db_, "SELECT id,name,item_type FROM menu_items", -1, &allm, nullptr);
        while (sqlite3_step(allm) == SQLITE_ROW) {
            int mid = sqlite3_column_int(allm, 0);
            std::string name = reinterpret_cast<const char*>(sqlite3_column_text(allm, 1));
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(allm, 2));
            auto link = [&](int mod) {
                sqlite3_stmt* l = nullptr;
                sqlite3_prepare_v2(db_, "INSERT OR IGNORE INTO menu_item_modifiers(menu_item_id,modifier_id) VALUES(?,?)", -1, &l, nullptr);
                sqlite3_bind_int(l, 1, mid);
                sqlite3_bind_int(l, 2, mod);
                sqlite3_step(l);
                sqlite3_finalize(l);
            };
            if (name.find("Latte") != std::string::npos || name.find("Cappuccino") != std::string::npos ||
                name.find("Macchiato") != std::string::npos || name.find("Americano") != std::string::npos ||
                name.find("Espresso") != std::string::npos) {
                link(mExtraShot); link(mOat); link(mVanilla); link(mNoSugar);
            }
            if (name.find("Sandvic") != std::string::npos) link(mExtraCheese);
            if (type == "drink") link(mNoSugar);
        }
        sqlite3_finalize(allm);

        set_setting("tax_rate", "0.10");
        set_setting("service_rate", "0.00");
        set_setting("restaurant_name", "RestoPulse Cafe");
        set_setting("currency", "USD");
        exec("INSERT INTO meta(key,value) VALUES('seeded','1')");
        exec("INSERT INTO meta(key,value) VALUES('schema_version','1')");

        commit();
    } catch (...) {
        rollback();
        throw;
    }
}

} // namespace rp
