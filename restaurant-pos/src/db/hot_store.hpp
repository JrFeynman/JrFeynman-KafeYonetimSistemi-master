#pragma once
#include "core/types.hpp"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <deque>
#include <string>
#include <vector>

namespace rp {

// In-process hot engine. Mirrors Redis key design so a Redis backend can plug in.
// Thread-safe; intended for UI + service threads.
class HotStore {
public:
    using EventFn = std::function<void(const std::string& channel, const std::string& payload)>;

    void set_table_status(int table_id, TableStatus st);
    TableStatus table_status(int table_id) const;
    void set_session_snapshot(int table_id, const std::string& json);
    std::string session_snapshot(int table_id) const;
    void clear_session(int table_id);

    void set_active_staff(int staff_id, const std::string& name, Role role);
    int active_staff_id() const;
    std::string active_staff_name() const;
    Role active_staff_role() const;

    void enqueue_ticket(const KitchenTicket& t);
    std::vector<KitchenTicket> tickets() const;
    bool update_ticket(int id, TicketStatus st);
    void remove_ticket(int id);

    void mark_low_stock(int ingredient_id, bool low);
    std::vector<int> low_stock_ids() const;

    void publish(const std::string& channel, const std::string& payload);
    void subscribe(EventFn fn);

    // Optional Redis bridge stats
    void set_redis_connected(bool v) { redis_connected_ = v; }
    bool redis_connected() const { return redis_connected_; }

private:
    mutable std::mutex mu_;
    std::unordered_map<int, TableStatus> table_status_;
    std::unordered_map<int, std::string> session_json_;
    int staff_id_ = 0;
    std::string staff_name_;
    Role staff_role_ = Role::Waiter;
    std::deque<KitchenTicket> tickets_;
    std::unordered_set<int> low_stock_;
    std::vector<EventFn> listeners_;
    bool redis_connected_ = false;
};

// Optional Redis adapter (compiled when RESTOPULSE_HAS_REDIS is defined).
class RedisHotBridge {
public:
    bool connect(const std::string& host, int port);
    void disconnect();
    bool ok() const { return ok_; }
    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    void publish(const std::string& channel, const std::string& payload);
private:
    bool ok_ = false;
    void* ctx_ = nullptr; // redisContext*
};

} // namespace rp
