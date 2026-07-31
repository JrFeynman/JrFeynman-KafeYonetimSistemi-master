#include "db/hub.hpp"
#include "services/pos_app.hpp"
#include "ui/app_ui.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <cstdlib>

namespace fs = std::filesystem;

static void run_smoke(rp::PosApp& app) {
    std::cout << "=== RestoPulse smoke test ===\n";
    if (!app.login("0000")) {
        std::cerr << "login failed\n";
        std::exit(1);
    }
    std::cout << "Login OK: " << app.current().name << "\n";
    auto tables = app.tables();
    std::cout << "Tables: " << tables.size() << "\n";
    if (tables.size() != 10) {
        std::cerr << "expected 10 tables\n";
        std::exit(1);
    }
    int sid = app.open_table(1, 2, "Smoke Tester");
    std::cout << "Session " << sid << " on T1\n";
    auto menu = app.menu();
    std::cout << "Menu items: " << menu.size() << "\n";
    // pick latte medium-ish and sandwich
    std::vector<rp::CartLine> cart;
    for (const auto& m : menu) {
        if (m.name.find("Latte (Medium)") != std::string::npos ||
            m.name.find("Sandvic") != std::string::npos) {
            rp::CartLine cl;
            cl.menu_item_id = m.id;
            cl.name = m.name;
            cl.qty = 1;
            cl.unit_price = m.base_price;
            cart.push_back(cl);
        }
        if (cart.size() >= 2) break;
    }
    auto res = app.place_order(sid, cart, true, "smoke");
    if (!res.ok) {
        std::cerr << "place_order failed: " << res.error << "\n";
        std::exit(1);
    }
    std::cout << "Order " << res.order_id << " tickets=" << res.ticket_ids.size() << "\n";
    auto tickets = app.kitchen_tickets();
    std::cout << "Open tickets: " << tickets.size() << "\n";
    for (auto& t : tickets) {
        app.ticket_start(t.id);
        app.ticket_done(t.id);
    }
    // compute pay amount roughly
    double due = 0;
    auto sess = app.session_for_table(1);
    if (sess) {
        for (auto& o : sess->orders)
            for (auto& l : o.lines) due += l.unit_price * l.qty;
    }
    double total = due * 1.10;
    auto pay = app.pay(sid, 0, 1.0, rp::PayMethod::Card, total + 1.0, "Smoke Tester");
    if (!pay.ok) {
        std::cerr << "pay failed: " << pay.error << "\n";
        std::exit(1);
    }
    std::cout << "Receipt " << pay.receipt.receipt_no << " total=$" << pay.receipt.total << "\n";
    auto stock = app.stock();
    int low = 0;
    for (auto& i : stock) if (i.low()) ++low;
    std::cout << "Ingredients: " << stock.size() << " low=" << low << "\n";
    auto rep = app.report_today();
    std::cout << "Day report revenue=$" << rep.revenue << " receipts=" << rep.receipts << "\n";
    std::cout << "SMOKE OK\n";
    app.logout();
}

int main(int argc, char** argv) {
    bool smoke = false;
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--smoke") smoke = true;
        if (a == "--headless") headless = true;
    }

    fs::path data_dir = fs::path("data");
    fs::create_directories(data_dir);
    fs::path db = data_dir / "restopulse.db";

    const char* redis_host = std::getenv("RESTOPULSE_REDIS_HOST");
    if (!redis_host) redis_host = "127.0.0.1";
    int redis_port = 6379;
    if (const char* p = std::getenv("RESTOPULSE_REDIS_PORT")) redis_port = std::atoi(p);

    try {
        rp::DatabaseHub hub(db.string(), redis_host, redis_port);
        hub.init();
        rp::PosApp app(hub);
        std::cout << app.status_line() << "\n";
        std::cout << "DB: " << db << "\n";

        if (smoke || headless) {
            run_smoke(app);
            if (headless || smoke) return 0;
        }
        return rp::run_ui(app);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
