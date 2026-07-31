#include "db/hub.hpp"
#include <sstream>

namespace rp {

DatabaseHub::DatabaseHub(std::string db_path, std::string redis_host, int redis_port)
    : db_path_(std::move(db_path)), redis_host_(std::move(redis_host)), redis_port_(redis_port) {}

void DatabaseHub::init() {
    ledger_ = std::make_unique<Ledger>(db_path_);
    ledger_->open();
    if (!ledger_->is_seeded()) ledger_->seed_full_mock();
    ledger_->ensure_role_policy();

    if (redis_.connect(redis_host_, redis_port_)) {
        hot_.set_redis_connected(true);
    } else {
        hot_.set_redis_connected(false);
    }
    sync_hot_from_ledger();
    refresh_low_stock_flags();
}

void DatabaseHub::sync_hot_from_ledger() {
    for (const auto& t : ledger_->list_tables()) {
        hot_.set_table_status(t.id, t.status);
        if (t.open_session_id) {
            std::ostringstream oss;
            oss << "{\"session_id\":" << t.open_session_id
                << ",\"customer\":\"" << t.customer_name
                << "\",\"covers\":" << t.covers
                << ",\"total\":" << t.running_total << "}";
            hot_.set_session_snapshot(t.id, oss.str());
            if (redis_.ok()) {
                redis_.set("table:" + std::to_string(t.id) + ":status", to_string(t.status));
                redis_.set("table:" + std::to_string(t.id) + ":session", oss.str());
            }
        } else {
            hot_.clear_session(t.id);
        }
    }
    auto tickets = ledger_->load_open_tickets();
    // rebuild queue
    for (const auto& existing : hot_.tickets()) hot_.remove_ticket(existing.id);
    for (const auto& kt : tickets) hot_.enqueue_ticket(kt);
}

void DatabaseHub::publish_table(int table_id) {
    auto tables = ledger_->list_tables();
    for (const auto& t : tables) {
        if (t.id != table_id) continue;
        hot_.set_table_status(t.id, t.status);
        if (redis_.ok()) {
            redis_.set("table:" + std::to_string(t.id) + ":status", to_string(t.status));
            redis_.publish("restopulse:events:table", std::to_string(t.id));
        }
        break;
    }
}

void DatabaseHub::refresh_low_stock_flags() {
    for (const auto& i : ledger_->ingredients()) {
        hot_.mark_low_stock(i.id, i.low());
        if (redis_.ok() && i.low()) {
            redis_.publish("restopulse:events:stock", i.sku);
        }
    }
}

} // namespace rp
