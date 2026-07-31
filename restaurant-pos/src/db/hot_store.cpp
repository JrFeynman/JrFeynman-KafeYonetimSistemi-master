#include "db/hot_store.hpp"
#include <algorithm>

namespace rp {

void HotStore::set_table_status(int table_id, TableStatus st) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        table_status_[table_id] = st;
    }
    publish("restopulse:events:table", std::to_string(table_id) + ":" + to_string(st));
}

TableStatus HotStore::table_status(int table_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = table_status_.find(table_id);
    return it == table_status_.end() ? TableStatus::Free : it->second;
}

void HotStore::set_session_snapshot(int table_id, const std::string& json) {
    std::lock_guard<std::mutex> lock(mu_);
    session_json_[table_id] = json;
}

std::string HotStore::session_snapshot(int table_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = session_json_.find(table_id);
    return it == session_json_.end() ? "" : it->second;
}

void HotStore::clear_session(int table_id) {
    std::lock_guard<std::mutex> lock(mu_);
    session_json_.erase(table_id);
}

void HotStore::set_active_staff(int staff_id, const std::string& name, Role role) {
    std::lock_guard<std::mutex> lock(mu_);
    staff_id_ = staff_id;
    staff_name_ = name;
    staff_role_ = role;
}

int HotStore::active_staff_id() const {
    std::lock_guard<std::mutex> lock(mu_);
    return staff_id_;
}
std::string HotStore::active_staff_name() const {
    std::lock_guard<std::mutex> lock(mu_);
    return staff_name_;
}
Role HotStore::active_staff_role() const {
    std::lock_guard<std::mutex> lock(mu_);
    return staff_role_;
}

void HotStore::enqueue_ticket(const KitchenTicket& t) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        tickets_.push_back(t);
    }
    publish("restopulse:events:kitchen", "ticket:" + std::to_string(t.id));
}

std::vector<KitchenTicket> HotStore::tickets() const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<KitchenTicket>(tickets_.begin(), tickets_.end());
}

bool HotStore::update_ticket(int id, TicketStatus st) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& t : tickets_) {
            if (t.id == id) {
                t.status = st;
                found = true;
                break;
            }
        }
    }
    if (found) publish("restopulse:events:kitchen", "update:" + std::to_string(id));
    return found;
}

void HotStore::remove_ticket(int id) {
    std::lock_guard<std::mutex> lock(mu_);
    tickets_.erase(std::remove_if(tickets_.begin(), tickets_.end(),
        [&](const KitchenTicket& t){ return t.id == id; }), tickets_.end());
}

void HotStore::mark_low_stock(int ingredient_id, bool low) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (low) low_stock_.insert(ingredient_id);
        else low_stock_.erase(ingredient_id);
    }
    publish("restopulse:events:stock", std::to_string(ingredient_id));
}

std::vector<int> HotStore::low_stock_ids() const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<int>(low_stock_.begin(), low_stock_.end());
}

void HotStore::publish(const std::string& channel, const std::string& payload) {
    // listeners called without holding lock to avoid deadlocks — copy first
    std::vector<EventFn> copy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        copy = listeners_;
    }
    for (auto& fn : copy) fn(channel, payload);
}

void HotStore::subscribe(EventFn fn) {
    std::lock_guard<std::mutex> lock(mu_);
    listeners_.push_back(std::move(fn));
}

#if defined(RESTOPULSE_HAS_REDIS)
#include <hiredis/hiredis.h>
#include <sys/time.h>

bool RedisHotBridge::connect(const std::string& host, int port) {
    disconnect();
    // Short timeout so offline Redis never blocks app startup.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200 ms
    auto* c = redisConnectWithTimeout(host.c_str(), port, tv);
    if (!c || c->err) {
        if (c) redisFree(c);
        ok_ = false;
        ctx_ = nullptr;
        return false;
    }
    redisSetTimeout(c, tv);
    ctx_ = c;
    ok_ = true;
    return true;
}

void RedisHotBridge::disconnect() {
    if (ctx_) {
        redisFree(static_cast<redisContext*>(ctx_));
        ctx_ = nullptr;
    }
    ok_ = false;
}

void RedisHotBridge::set(const std::string& key, const std::string& value) {
    if (!ok_ || !ctx_) return;
    auto* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), "SET %s %s", key.c_str(), value.c_str()));
    if (reply) freeReplyObject(reply);
}

std::string RedisHotBridge::get(const std::string& key) {
    if (!ok_ || !ctx_) return {};
    auto* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), "GET %s", key.c_str()));
    std::string out;
    if (reply && reply->type == REDIS_REPLY_STRING) out.assign(reply->str, reply->len);
    if (reply) freeReplyObject(reply);
    return out;
}

void RedisHotBridge::publish(const std::string& channel, const std::string& payload) {
    if (!ok_ || !ctx_) return;
    auto* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), "PUBLISH %s %s", channel.c_str(), payload.c_str()));
    if (reply) freeReplyObject(reply);
}
#else
bool RedisHotBridge::connect(const std::string&, int) { ok_ = false; return false; }
void RedisHotBridge::disconnect() { ok_ = false; }
void RedisHotBridge::set(const std::string&, const std::string&) {}
std::string RedisHotBridge::get(const std::string&) { return {}; }
void RedisHotBridge::publish(const std::string&, const std::string&) {}
#endif

} // namespace rp
