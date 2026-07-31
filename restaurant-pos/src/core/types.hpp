#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cmath>

namespace rp {

inline std::string now_iso() {
    using namespace std::chrono;
    const auto t = system_clock::to_time_t(system_clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

enum class Role { Waiter, Cashier, Kitchen, Manager };
enum class TableStatus { Free, Occupied, Ordering, Waiting, Bill, Dirty };
enum class ItemType { Food, Drink, Combo };
enum class OrderStatus { Placed, Preparing, Ready, Served, Void };
enum class TicketStation { Kitchen, Bar };
enum class TicketStatus { Queued, InProgress, Done, Cancelled };
enum class PayMethod { Cash, Card, Mixed, Other };
enum class ReceiptStatus { Issued, Void, Refunded };
enum class LedgerReason { Seed, OrderConsume, VoidRestock, Waste, Receive, Adjustment };

inline const char* to_string(TableStatus s) {
    switch (s) {
        case TableStatus::Free: return "free";
        case TableStatus::Occupied: return "occupied";
        case TableStatus::Ordering: return "ordering";
        case TableStatus::Waiting: return "waiting";
        case TableStatus::Bill: return "bill";
        case TableStatus::Dirty: return "dirty";
    }
    return "free";
}
inline TableStatus table_status_from(const std::string& s) {
    if (s == "occupied") return TableStatus::Occupied;
    if (s == "ordering") return TableStatus::Ordering;
    if (s == "waiting") return TableStatus::Waiting;
    if (s == "bill") return TableStatus::Bill;
    if (s == "dirty") return TableStatus::Dirty;
    return TableStatus::Free;
}
inline const char* to_string(Role r) {
    switch (r) {
        case Role::Waiter: return "waiter";
        case Role::Cashier: return "cashier";
        case Role::Kitchen: return "kitchen";
        case Role::Manager: return "manager";
    }
    return "waiter";
}
inline Role role_from(const std::string& s) {
    if (s == "cashier") return Role::Cashier;
    if (s == "kitchen") return Role::Kitchen;
    if (s == "manager") return Role::Manager;
    return Role::Waiter;
}
inline const char* to_string(OrderStatus s) {
    switch (s) {
        case OrderStatus::Placed: return "placed";
        case OrderStatus::Preparing: return "preparing";
        case OrderStatus::Ready: return "ready";
        case OrderStatus::Served: return "served";
        case OrderStatus::Void: return "void";
    }
    return "placed";
}
inline OrderStatus order_status_from(const std::string& s) {
    if (s == "preparing") return OrderStatus::Preparing;
    if (s == "ready") return OrderStatus::Ready;
    if (s == "served") return OrderStatus::Served;
    if (s == "void") return OrderStatus::Void;
    return OrderStatus::Placed;
}
inline const char* to_string(PayMethod m) {
    switch (m) {
        case PayMethod::Cash: return "cash";
        case PayMethod::Card: return "card";
        case PayMethod::Mixed: return "mixed";
        case PayMethod::Other: return "other";
    }
    return "cash";
}
inline const char* to_string(LedgerReason r) {
    switch (r) {
        case LedgerReason::Seed: return "seed";
        case LedgerReason::OrderConsume: return "order_consume";
        case LedgerReason::VoidRestock: return "void_restock";
        case LedgerReason::Waste: return "waste";
        case LedgerReason::Receive: return "receive";
        case LedgerReason::Adjustment: return "adjustment";
    }
    return "adjustment";
}

struct Staff {
    int id = 0;
    std::string name;
    Role role = Role::Waiter;
    bool active = true;
};

struct DiningTable {
    int id = 0;
    std::string label;
    int seats = 4;
    float pos_x = 0, pos_y = 0;
    TableStatus status = TableStatus::Free;
    int open_session_id = 0; // 0 = none
    int covers = 0;
    std::string customer_name;
    double running_total = 0;
};

struct Ingredient {
    int id = 0;
    std::string sku;
    std::string name;
    std::string unit;
    double stock_qty = 0;
    double reorder_level = 0;
    double cost_per_unit = 0;
    std::string allergen_flags;
    bool low() const { return stock_qty <= reorder_level; }
};

struct RecipeLine {
    int ingredient_id = 0;
    std::string ingredient_name;
    std::string unit;
    double qty = 0;
};

struct MenuItem {
    int id = 0;
    int category_id = 0;
    std::string category;
    std::string name;
    double base_price = 0;
    ItemType type = ItemType::Food;
    std::string size_label;
    bool available = true;
    int prep_seconds = 300;
    std::string allergens;
    std::optional<double> happy_hour_price;
    bool is_combo = false;
    std::vector<RecipeLine> recipe;
    std::vector<int> modifier_ids;
    std::vector<int> combo_component_ids;
};

struct Modifier {
    int id = 0;
    std::string name;
    double price_delta = 0;
    int ingredient_id = 0;
    double ingredient_qty = 0;
};

struct OrderLine {
    int id = 0;
    int menu_item_id = 0;
    std::string name;
    int qty = 1;
    double unit_price = 0;
    OrderStatus status = OrderStatus::Placed;
    std::string note;
    int seat_no = 0;
    std::vector<int> modifier_ids;
    std::vector<std::string> modifier_names;
    double line_total() const {
        double mod = 0;
        // modifier price applied outside when building; unit_price already inclusive preferred
        return unit_price * qty + mod;
    }
};

struct Order {
    int id = 0;
    int session_id = 0;
    std::string placed_at;
    OrderStatus status = OrderStatus::Placed;
    int priority = 0;
    int placed_by = 0;
    std::string note;
    std::vector<OrderLine> lines;
};

struct Session {
    int id = 0;
    int table_id = 0;
    std::string opened_at;
    std::string closed_at;
    int covers = 2;
    std::string customer_name;
    int waiter_id = 0;
    std::string status = "open";
    std::string notes;
    std::vector<Order> orders;
};

struct KitchenTicket {
    int id = 0;
    int order_id = 0;
    int table_id = 0;
    std::string table_label;
    TicketStation station = TicketStation::Kitchen;
    std::string created_at;
    TicketStatus status = TicketStatus::Queued;
    int priority = 0;
    std::vector<OrderLine> lines;
    std::string note;
};

struct ReceiptPayment {
    PayMethod method = PayMethod::Cash;
    double amount = 0;
};

struct Receipt {
    int id = 0;
    int session_id = 0;
    std::string issued_at;
    double subtotal = 0;
    double tax = 0;
    double service_charge = 0;
    double discount = 0;
    double tip = 0;
    double total = 0;
    ReceiptStatus status = ReceiptStatus::Issued;
    std::string receipt_no;
    int cashier_id = 0;
    int print_count = 1;
    std::string customer_name;
    std::vector<std::pair<std::string, std::pair<int, double>>> lines; // desc, qty, unit
    std::vector<ReceiptPayment> payments;
};

struct WaitlistEntry {
    int id = 0;
    std::string name;
    int party_size = 2;
    std::string phone;
    std::string created_at;
    std::string status = "waiting";
    int estimated_wait_min = 15;
};

struct Loyalty {
    std::string customer_name;
    int stamps = 0;
    int points = 0;
};

struct DayReport {
    std::string day;
    int receipts = 0;
    double revenue = 0;
    double tax = 0;
    double tips = 0;
    int covers = 0;
    int voids = 0;
    std::vector<std::pair<std::string, int>> top_sellers;
    std::vector<std::pair<std::string, double>> ingredient_usage;
};

struct CartLine {
    int menu_item_id = 0;
    std::string name;
    int qty = 1;
    double unit_price = 0;
    std::vector<int> modifier_ids;
    std::vector<std::string> modifier_names;
    std::string note;
    int seat_no = 0;
    bool rush = false;
};

inline double round2(double v) {
    return std::round(v * 100.0) / 100.0;
}

} // namespace rp
