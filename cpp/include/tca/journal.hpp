// SQLite-backed trade journal with a strict order state machine and
// unique constraints that make duplicate order intents impossible.
#pragma once

#include <sqlite3.h>

#include <string>

#include "tca/common.hpp"

namespace tca {

bool valid_order_transition(const std::string& from, const std::string& to);
bool is_terminal_state(const std::string& s);

class Journal {
 public:
  explicit Journal(const std::string& path);
  ~Journal();
  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;

  void init();
  // Creates an order intent in PROPOSED state. Throws ValidationError on a
  // duplicate (mode, intent_key) so the same signal can never be ordered twice.
  json propose(const json& order, int64_t now);
  json transition(const std::string& client_order_id, const std::string& to, int64_t now, const json& detail);
  json record_fill(const std::string& client_order_id, double qty, double price, const std::string& fill_id, int64_t now);
  json close_trade(const json& trade, int64_t now);
  void audit(const std::string& kind, const json& detail, int64_t now);
  json open_orders(const std::string& mode);
  int unknown_count(const std::string& mode);
  json report(const std::string& mode, int64_t since);
  void kv_set(const std::string& k, const std::string& v);
  std::string kv_get(const std::string& k, const std::string& dflt);

 private:
  sqlite3* db_ = nullptr;
  void exec(const std::string& sql);
};

json run_journal_action(const json& in);

}  // namespace tca
