#pragma once
#include "db/ledger.hpp"
#include "db/hot_store.hpp"
#include <memory>
#include <string>

namespace rp {

// Unified facade over Hot + Ledger engines.
class DatabaseHub {
public:
    DatabaseHub(std::string db_path, std::string redis_host = "127.0.0.1", int redis_port = 6379);

    void init();
    Ledger& ledger() { return *ledger_; }
    HotStore& hot() { return hot_; }
    RedisHotBridge& redis() { return redis_; }

    void sync_hot_from_ledger();
    void publish_table(int table_id);
    void refresh_low_stock_flags();

private:
    std::string db_path_;
    std::string redis_host_;
    int redis_port_;
    std::unique_ptr<Ledger> ledger_;
    HotStore hot_;
    RedisHotBridge redis_;
};

} // namespace rp
