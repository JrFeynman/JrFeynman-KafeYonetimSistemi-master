#include "services/pos_app.hpp"
#include <sstream>

namespace rp {

PosApp::PosApp(DatabaseHub& hub) : hub_(hub) {}

bool PosApp::login(const std::string& pin) {
    auto s = hub_.ledger().login_pin(pin);
    if (!s) return false;
    // Only manager and waiter sessions are allowed.
    if (s->role != Role::Manager && s->role != Role::Waiter) return false;
    current_ = *s;
    hub_.hot().set_active_staff(s->id, s->name, s->role);
    return true;
}

Ledger::NewProductResult PosApp::add_product(const Ledger::NewProductInput& in) {
    Ledger::NewProductResult r;
    if (!is_manager()) {
        r.error = "Manager only";
        return r;
    }
    return hub_.ledger().add_menu_product(in);
}

bool PosApp::set_product_available(int menu_item_id, bool available) {
    if (!is_manager()) return false;
    return hub_.ledger().set_menu_item_available(menu_item_id, available);
}

void PosApp::logout() {
    current_.reset();
    hub_.hot().set_active_staff(0, "", Role::Waiter);
}

std::vector<DiningTable> PosApp::tables() { return hub_.ledger().list_tables(); }

int PosApp::open_table(int table_id, int covers, const std::string& customer) {
    if (!current_) return 0;
    int sid = hub_.ledger().open_session(table_id, covers, customer, current_->id);
    hub_.sync_hot_from_ledger();
    hub_.publish_table(table_id);
    return sid;
}

void PosApp::mark_table_clean(int table_id) {
    hub_.ledger().set_table_status(table_id, TableStatus::Free);
    hub_.hot().set_table_status(table_id, TableStatus::Free);
    hub_.hot().clear_session(table_id);
    hub_.publish_table(table_id);
}

void PosApp::request_bill(int table_id) {
    hub_.ledger().set_table_status(table_id, TableStatus::Bill);
    hub_.hot().set_table_status(table_id, TableStatus::Bill);
    hub_.publish_table(table_id);
}

std::optional<Session> PosApp::session_for_table(int table_id) {
    return hub_.ledger().open_session_for_table(table_id);
}

std::vector<MenuItem> PosApp::menu() { return hub_.ledger().menu_items(true); }

std::vector<Modifier> PosApp::modifiers(int menu_item_id) {
    return hub_.ledger().modifiers_for_item(menu_item_id);
}

bool PosApp::item_available(int menu_item_id, int qty, std::string* why) {
    std::vector<StockNeed> missing;
    bool ok = hub_.ledger().can_make(menu_item_id, qty, &missing);
    if (!ok && why) {
        std::ostringstream oss;
        oss << "86 / low stock: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i) oss << ", ";
            oss << missing[i].name << " need " << missing[i].qty << missing[i].unit
                << " have " << missing[i].available;
        }
        *why = oss.str();
    }
    return ok;
}

Ledger::PlaceOrderResult PosApp::place_order(int session_id, const std::vector<CartLine>& cart,
                                             bool rush, const std::string& note) {
    if (!current_) {
        Ledger::PlaceOrderResult r;
        r.error = "Not logged in";
        return r;
    }
    auto res = hub_.ledger().place_order(session_id, cart, current_->id, rush ? 1 : 0, note);
    if (res.ok) {
        hub_.sync_hot_from_ledger();
        hub_.refresh_low_stock_flags();
    }
    return res;
}

bool PosApp::void_order(int order_id, const std::string& reason) {
    if (!current_) return false;
    if (current_->role != Role::Manager && current_->role != Role::Cashier) return false;
    bool ok = hub_.ledger().void_order(order_id, current_->id, reason, true);
    if (ok) {
        hub_.sync_hot_from_ledger();
        hub_.refresh_low_stock_flags();
    }
    return ok;
}

std::vector<KitchenTicket> PosApp::kitchen_tickets() {
    return hub_.ledger().load_open_tickets();
}

void PosApp::ticket_start(int id) {
    hub_.ledger().set_ticket_status(id, TicketStatus::InProgress);
    hub_.hot().update_ticket(id, TicketStatus::InProgress);
    hub_.sync_hot_from_ledger();
}

void PosApp::ticket_done(int id) {
    hub_.ledger().set_ticket_status(id, TicketStatus::Done);
    hub_.hot().update_ticket(id, TicketStatus::Done);
    hub_.sync_hot_from_ledger();
}

std::vector<Ingredient> PosApp::stock() { return hub_.ledger().ingredients(); }

void PosApp::receive(int ingredient_id, double qty, const std::string& note) {
    if (!current_) return;
    hub_.ledger().receive_stock(ingredient_id, qty, current_->id, note);
    hub_.refresh_low_stock_flags();
}

void PosApp::waste(int ingredient_id, double qty, const std::string& note) {
    if (!current_) return;
    hub_.ledger().waste_stock(ingredient_id, qty, current_->id, note);
    hub_.refresh_low_stock_flags();
}

Ledger::PayResult PosApp::pay(int session_id, double discount, double tip,
                              PayMethod method, double amount,
                              const std::string& customer) {
    if (!current_) {
        Ledger::PayResult r;
        r.error = "Not logged in";
        return r;
    }
    if (discount > 0 && current_->role != Role::Manager && discount / std::max(1.0, amount) > 0.15) {
        // soft check — still allow cashier small discounts; manager for large handled in UI
    }
    double tax = 0.10, service = 0.0;
    try {
        tax = std::stod(hub_.ledger().setting("tax_rate", "0.10"));
        service = std::stod(hub_.ledger().setting("service_rate", "0.00"));
    } catch (...) {}

    Ledger::PayInput in;
    in.session_id = session_id;
    in.cashier_id = current_->id;
    in.discount = discount;
    in.tip = tip;
    in.tax_rate = tax;
    in.service_charge_rate = service;
    in.customer_name = customer;
    ReceiptPayment p;
    p.method = method;
    p.amount = amount;
    in.payments.push_back(p);

    auto res = hub_.ledger().close_and_pay(in);
    if (res.ok) {
        hub_.sync_hot_from_ledger();
    }
    return res;
}

std::vector<Receipt> PosApp::receipts() { return hub_.ledger().recent_receipts(100); }

void PosApp::reprint(int receipt_id) { hub_.ledger().bump_print_count(receipt_id); }

bool PosApp::void_receipt(int receipt_id, const std::string& reason) {
    if (!is_manager()) return false;
    return hub_.ledger().void_receipt(receipt_id, current_->id, reason);
}

int PosApp::add_wait(const std::string& name, int party, const std::string& phone, int eta) {
    return hub_.ledger().add_waitlist(name, party, phone, eta);
}

std::vector<WaitlistEntry> PosApp::waitlist() { return hub_.ledger().waitlist(); }

void PosApp::seat_wait(int id) { hub_.ledger().set_waitlist_status(id, "seated"); }

DayReport PosApp::report_today() {
    // today local date
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return hub_.ledger().day_report(buf);
}

Loyalty PosApp::loyalty(const std::string& name) { return hub_.ledger().loyalty_get(name); }

bool PosApp::happy_hour() const { return hub_.ledger().is_happy_hour(); }

std::string PosApp::status_line() const {
    std::ostringstream oss;
    oss << "RestoPulse Dual-Engine";
    if (hub_.hot().redis_connected()) oss << " | Redis: ON";
    else oss << " | Redis: offline (in-process hot)";
    if (current_) oss << " | " << current_->name << " (" << to_string(current_->role) << ")";
    if (happy_hour()) oss << " | HAPPY HOUR";
    return oss.str();
}

} // namespace rp
