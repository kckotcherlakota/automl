#include <gtest/gtest.h>
#include <cstdio>
#include "tca/journal.hpp"
using namespace tca;

namespace {
struct TmpDb {
  std::string path;
  TmpDb() { path = std::string(::testing::TempDir()) + "journal_" + std::to_string(rand()) + ".db"; }
  ~TmpDb() { std::remove(path.c_str()); }
};
json order(const std::string& id, const std::string& intent) {
  return {{"client_order_id", id}, {"mode", "PAPER"}, {"intent_key", intent}, {"symbol", "BTC-USD"},
          {"asset_class", "crypto"}, {"qty", 0.001}, {"limit_price", 75000}};
}
}  // namespace

TEST(Journal, StateMachineTransitions) {
  EXPECT_TRUE(valid_order_transition("PROPOSED", "REVIEWED"));
  EXPECT_FALSE(valid_order_transition("PROPOSED", "FILLED"));
  EXPECT_FALSE(valid_order_transition("FILLED", "CANCELLED"));
  EXPECT_TRUE(valid_order_transition("UNKNOWN", "FILLED"));
  EXPECT_TRUE(is_terminal_state("REJECTED"));
}

TEST(Journal, DuplicateIntentRejected) {
  TmpDb t; Journal j(t.path); j.init();
  j.propose(order("a", "sig-1"), 1);
  EXPECT_THROW(j.propose(order("b", "sig-1"), 2), ValidationError);
  EXPECT_THROW(j.propose(order("a", "sig-2"), 2), ValidationError);
}

TEST(Journal, ResearchModeOrdersRefused) {
  TmpDb t; Journal j(t.path); j.init();
  json o = order("a", "x"); o["mode"] = "RESEARCH";
  EXPECT_THROW(j.propose(o, 1), ValidationError);
}

TEST(Journal, LifecyclePartialFillsAndUnknown) {
  TmpDb t; Journal j(t.path); j.init();
  j.propose(order("a", "sig-1"), 1);
  EXPECT_THROW(j.transition("a", "FILLED", 2, json::object()), ValidationError);
  j.transition("a", "REVIEWED", 2, json::object());
  j.transition("a", "APPROVED_BY_RISK", 3, json::object());
  j.transition("a", "SUBMITTED", 4, json{{"broker_order_id", "B1"}});
  j.transition("a", "UNKNOWN", 5, json{{"why", "timeout"}});
  EXPECT_EQ(j.unknown_count("PAPER"), 1);
  j.transition("a", "PARTIALLY_FILLED", 6, json::object());
  auto f = j.record_fill("a", 0.0004, 75000, "f1", 6);
  EXPECT_FALSE(f["complete"].get<bool>());
  EXPECT_THROW(j.record_fill("a", 0.0004, 75000, "f1", 7), ValidationError);   // duplicate fill id
  EXPECT_THROW(j.record_fill("a", 0.0010, 75000, "f2", 7), ValidationError);   // overfill
  auto g = j.record_fill("a", 0.0006, 75100, "f3", 8);
  EXPECT_TRUE(g["complete"].get<bool>());
  EXPECT_NEAR(g["avg_fill_price"].get<double>(), 75060, 1e-6);
  j.transition("a", "FILLED", 9, json::object());
  EXPECT_EQ(j.unknown_count("PAPER"), 0);
  EXPECT_TRUE(j.open_orders("PAPER").empty());
}

TEST(Journal, TradesAndReport) {
  TmpDb t; Journal j(t.path); j.init();
  j.close_trade({{"mode", "PAPER"}, {"symbol", "ETH-USD"}, {"strategy", "E_CRYPTO_TREND"}, {"qty", 0.1},
                 {"entry", 2000}, {"exit", 2040}, {"pnl", 4}, {"entry_order_id", "o1"}}, 10);
  EXPECT_THROW(j.close_trade({{"mode", "PAPER"}, {"symbol", "ETH-USD"}, {"qty", 0.1}, {"entry", 1},
                              {"exit", 1}, {"pnl", 0}, {"entry_order_id", "o1"}}, 11), ValidationError);
  auto r = j.report("PAPER", 0);
  EXPECT_EQ(r["count"], 1);
  EXPECT_DOUBLE_EQ(r["net_pnl"].get<double>(), 4);
  j.kv_set("risk_mode", "LOCKED");
  EXPECT_EQ(j.kv_get("risk_mode", "NORMAL"), "LOCKED");
}
