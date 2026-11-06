#include <gtest/gtest.h>
#include "tca/execution.hpp"
using namespace tca;

namespace {
Quote q(double bid, double ask, int64_t ts = 100, double bs = 100, double as = 100) { return Quote{bid, ask, bs, as, ts}; }
bool has(const ExecPlan& p, const std::string& s) {
  for (auto& r : p.reasons) if (r.find(s) != std::string::npos) return true;
  return false;
}
}  // namespace

TEST(Execution, LimitPriceBetweenMidAndAskRoundedDown) {
  ExecConfig c;
  auto p = plan_entry(c, q(100.00, 100.04), 100.0, 0.01, 101, 0);
  ASSERT_TRUE(p.ok);
  EXPECT_DOUBLE_EQ(p.limit_price, 100.02);  // mid 100.02 + 0.25*0.02 = 100.025 -> 100.02
  EXPECT_LE(p.limit_price, 100.04);
}

TEST(Execution, RejectsStaleWideAndChasing) {
  ExecConfig c;
  EXPECT_TRUE(has(plan_entry(c, q(100, 100.02, 50), 100, 0.01, 100, 0), "stale"));
  EXPECT_TRUE(has(plan_entry(c, q(100, 100.5), 100, 0.01, 101, 0), "spread"));
  EXPECT_TRUE(has(plan_entry(c, q(100.5, 100.52), 100, 0.01, 101, 0), "chase"));
}

TEST(Execution, MicrostructureFilterAndRetries) {
  ExecConfig c;
  EXPECT_TRUE(has(plan_entry(c, q(100, 100.02, 100, 10, 500), 100, 0.01, 101, 0), "ask-heavy"));
  EXPECT_TRUE(has(plan_entry(c, q(100, 100.02), 100, 0.01, 101, 5), "retries"));
}

TEST(Execution, QuoteChangedBetweenReviewAndSubmit) {
  ExecConfig c;  // review at 100.0, submit-time quote jumped 0.6%
  auto p = plan_entry(c, q(100.59, 100.61), 100.0, 0.01, 101, 0);
  EXPECT_FALSE(p.ok);
}

TEST(Execution, InvalidQuotesAndTicks) {
  EXPECT_THROW(parse_quote(json{{"bid", 10}, {"ask", 9}, {"ts", 1}}), ValidationError);
  EXPECT_THROW(round_to_tick(1, 0, true), ValidationError);
  EXPECT_DOUBLE_EQ(round_to_tick(1.234, 0.01, true), 1.23);
  EXPECT_DOUBLE_EQ(round_to_tick(1.231, 0.01, false), 1.24);
}
