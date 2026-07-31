#pragma once
#include "db/hub.hpp"
#include <string>
#include <vector>
#include <optional>

namespace rp {

// High-level POS operations used by the UI.
class PosApp {
public:
    explicit PosApp(DatabaseHub& hub);

    bool login(const std::string& pin);
    void logout();
    bool logged_in() const { return !!current_; }
    const Staff& current() const { return *current_; }
    bool is_manager() const { return current_ && current_->role == Role::Manager; }
    bool is_waiter() const { return current_ && current_->role == Role::Waiter; }

    // Manager: create menu product (+ optional recipe)
    Ledger::NewProductResult add_product(const Ledger::NewProductInput& in);
    bool set_product_available(int menu_item_id, bool available);

    std::vector<DiningTable> tables();
    int open_table(int table_id, int covers, const std::string& customer);
    void mark_table_clean(int table_id);
    void request_bill(int table_id);
    std::optional<Session> session_for_table(int table_id);

    std::vector<MenuItem> menu();
    std::vector<Modifier> modifiers(int menu_item_id);
    bool item_available(int menu_item_id, int qty, std::string* why);

    Ledger::PlaceOrderResult place_order(int session_id, const std::vector<CartLine>& cart,
                                         bool rush, const std::string& note);
    bool void_order(int order_id, const std::string& reason);

    std::vector<KitchenTicket> kitchen_tickets();
    void ticket_start(int id);
    void ticket_done(int id);

    std::vector<Ingredient> stock();
    void receive(int ingredient_id, double qty, const std::string& note);
    void waste(int ingredient_id, double qty, const std::string& note);

    Ledger::PayResult pay(int session_id, double discount, double tip,
                          PayMethod method, double amount,
                          const std::string& customer);
    std::vector<Receipt> receipts();
    void reprint(int receipt_id);
    bool void_receipt(int receipt_id, const std::string& reason);

    int add_wait(const std::string& name, int party, const std::string& phone, int eta);
    std::vector<WaitlistEntry> waitlist();
    void seat_wait(int id);

    DayReport report_today();
    Loyalty loyalty(const std::string& name);

    bool happy_hour() const;
    std::string status_line() const;
    DatabaseHub& hub() { return hub_; }

private:
    DatabaseHub& hub_;
    std::optional<Staff> current_;
};

} // namespace rp
