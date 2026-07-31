#pragma once
#include "core/types.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <stdexcept>

namespace rp {

struct StockNeed {
    int ingredient_id = 0;
    std::string name;
    std::string unit;
    double qty = 0;
    double available = 0;
};

class Ledger {
public:
    explicit Ledger(std::string db_path);
    ~Ledger();

    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;

    void open();
    void close();
    void exec_script(const std::string& sql);
    void migrate_and_tune();

    // Auth
    std::optional<Staff> login_pin(const std::string& pin);
    std::vector<Staff> list_staff();

    // Floor
    std::vector<DiningTable> list_tables();
    void set_table_status(int table_id, TableStatus st);
    int open_session(int table_id, int covers, const std::string& customer, int waiter_id);
    void close_session(int session_id);
    std::optional<Session> open_session_for_table(int table_id);
    Session load_session(int session_id);

    // Menu / stock
    std::vector<std::pair<int,std::string>> categories();
    std::vector<MenuItem> menu_items(bool only_available = true);
    MenuItem menu_item(int id);
    std::vector<Modifier> modifiers_for_item(int menu_item_id);
    std::vector<Ingredient> ingredients();
    Ingredient ingredient(int id);
    void receive_stock(int ingredient_id, double qty, int staff_id, const std::string& note);
    void waste_stock(int ingredient_id, double qty, int staff_id, const std::string& note);
    void adjust_stock(int ingredient_id, double new_qty, int staff_id, const std::string& note);

    // Pricing
    double price_for(const MenuItem& item, bool happy_hour) const;
    bool is_happy_hour() const;
    void set_setting(const std::string& key, const std::string& value);
    std::string setting(const std::string& key, const std::string& def = "") const;

    // Order commit (atomic: lines + recipe explode + ledger + kitchen ticket rows)
    struct PlaceOrderResult {
        bool ok = false;
        std::string error;
        int order_id = 0;
        std::vector<int> ticket_ids;
        std::vector<StockNeed> missing;
    };
    PlaceOrderResult place_order(int session_id, const std::vector<CartLine>& cart,
                                 int staff_id, int priority, const std::string& note);

    bool void_order(int order_id, int staff_id, const std::string& reason, bool restock);
    bool set_order_status(int order_id, OrderStatus st);
    bool set_order_line_status(int line_id, OrderStatus st);

    std::vector<KitchenTicket> load_open_tickets();
    bool set_ticket_status(int ticket_id, TicketStatus st);

    // 86 check
    bool can_make(int menu_item_id, int qty, std::vector<StockNeed>* missing = nullptr);

    // Payments / receipts
    struct PayInput {
        int session_id = 0;
        int cashier_id = 0;
        double discount = 0;
        double tip = 0;
        double service_charge_rate = 0.0; // e.g. 0.10
        double tax_rate = 0.10;
        std::vector<ReceiptPayment> payments;
        std::string customer_name;
    };
    struct PayResult {
        bool ok = false;
        std::string error;
        Receipt receipt;
    };
    PayResult close_and_pay(const PayInput& in);
    std::optional<Receipt> receipt(int id);
    std::vector<Receipt> recent_receipts(int limit = 50);
    bool void_receipt(int receipt_id, int manager_id, const std::string& reason);
    void bump_print_count(int receipt_id);

    // Waitlist / loyalty
    int add_waitlist(const std::string& name, int party, const std::string& phone, int eta);
    std::vector<WaitlistEntry> waitlist();
    void set_waitlist_status(int id, const std::string& status);
    Loyalty loyalty_get(const std::string& name);
    void loyalty_add_stamps(const std::string& name, int stamps, int points);

    // Reports
    DayReport day_report(const std::string& day_yyyy_mm_dd);

    // Product management (manager)
    struct NewProductInput {
        int category_id = 0;
        std::string name;
        double base_price = 0;
        std::string item_type = "food"; // food | drink | combo
        std::string size_label;
        int prep_seconds = 180;
        std::string allergens;
        std::optional<double> happy_hour_price;
        // optional single-ingredient simple recipe (qty per 1 serving)
        int recipe_ingredient_id = 0;
        double recipe_qty = 0;
        // multi-line recipe
        std::vector<std::pair<int, double>> recipe_lines; // ingredient_id, qty
    };
    struct NewProductResult {
        bool ok = false;
        std::string error;
        int menu_item_id = 0;
    };
    NewProductResult add_menu_product(const NewProductInput& in);
    bool set_menu_item_available(int menu_item_id, bool available);

    // Keep only manager + waiter active accounts
    void ensure_role_policy();

    // Seed
    bool is_seeded() const;
    void seed_full_mock();

    sqlite3* raw() { return db_; }

private:
    std::string path_;
    sqlite3* db_ = nullptr;
    mutable std::mutex mu_;

    void begin();
    void commit();
    void rollback();
    int last_id();
    void exec(const std::string& sql);
    static int cb_noop(void*, int, char**, char**) { return 0; }

    void apply_ledger(int ingredient_id, double delta, LedgerReason reason,
                      const std::string& ref_type, int ref_id,
                      const std::string& note, int staff_id);
    std::vector<StockNeed> explode_recipe(int menu_item_id, int qty);
    void load_recipe_into(MenuItem& item);
    std::string next_receipt_no();
};

} // namespace rp
