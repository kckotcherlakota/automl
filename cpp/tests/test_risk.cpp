#include <gtest/gtest.h>
#include "tca/risk.hpp"
using namespace tca;

namespace {
AccountState acct(double eq = 10000) {
  AccountState a;
  a.equity = a.buying_power = eq;
  a.start_day_equity = a.start_week_equity = a.start_month_equity = a.high_water_mark = eq;
  a.now = 1'000'000; a.operating_mode = "PAPER";
  return a;
}
Proposal stock(const std::string& sym = "SPY", double entry = 100, double stop = 99) {
  Proposal p; p.symbol = sym; p.ac = AssetClass::Stock; p.entry = entry; p.stop = stop;
  p.qty_increment = 1; p.min_qty = 1; return p;
}
RiskConfig cfg() {
  RiskConfig c;
  c.correlation_groups = {{"us_tech", {"SPY", "QQQ", "NVDA", "AMD"}}, {"crypto", {"BTC-USD", "ETH-USD"}}};
  return c;
}
bool has(const RiskDecision& d, const std::string& s) {
  for (auto& r : d.reasons) if (r.find(s) != std::string::npos) return true;
  return false;
}
}  // namespace

TEST(Risk, SizesByRiskBudgetAndRoundsDown) {
  auto d = check_and_size(cfg(), acct(), stock("SPY", 100, 99));  // budget $25, risk $1/share
  ASSERT_TRUE(d.approved) << d.to_json().dump();
  EXPECT_DOUBLE_EQ(d.quantity, 10);  // capped by 10% notional ($1000 / $100), below 25 by risk
  auto d2 = check_and_size(cfg(), acct(), stock("SPY", 100, 96.5));  // risk 3.5 => 7.14 shares
  EXPECT_DOUBLE_EQ(d2.quantity, 7);
  EXPECT_LE(d2.risk_dollars, 25.0);
}

TEST(Risk, SlippageAndFeesIncludedInRiskPerUnit) {
  Proposal p = stock("SPY", 100, 96.5); p.slippage_per_unit = 0.5; p.fee_per_unit = 0.25;
  auto d = check_and_size(cfg(), acct(), p);
  EXPECT_DOUBLE_EQ(d.risk_per_unit, 4.25);
  EXPECT_DOUBLE_EQ(d.quantity, 5);  // floor(25/4.25 = 5.88)
}

TEST(Risk, FloorToIncrementNeverRoundsUp) {
  EXPECT_DOUBLE_EQ(floor_to_increment(7.9999, 1), 7);
  EXPECT_NEAR(floor_to_increment(0.00012345678, 1e-8), 0.00012345, 1e-12);
  EXPECT_LE(floor_to_increment(0.3, 0.1), 0.3);
  EXPECT_DOUBLE_EQ(floor_to_increment(-1, 1), 0);
}

TEST(Risk, ResearchModeNeverApproves) {
  auto a = acct(); a.operating_mode = "RESEARCH";
  EXPECT_TRUE(has(check_and_size(cfg(), a, stock()), "RESEARCH"));
}

TEST(Risk, KillSwitchAndUnknownOrdersBlock) {
  auto a = acct(); a.kill_switch = true;
  EXPECT_FALSE(check_and_size(cfg(), a, stock()).approved);
  auto b = acct(); b.unknown_orders = 1;
  EXPECT_TRUE(has(check_and_size(cfg(), b, stock()), "UNKNOWN"));
}

TEST(Risk, DailyWeeklyMonthlyLossLock) {
  auto a = acct(9890); a.start_day_equity = 10000; a.high_water_mark = 10000;  // -1.1% day
  auto d = check_and_size(cfg(), a, stock());
  EXPECT_EQ(d.mode, RiskMode::Locked);
  EXPECT_FALSE(d.approved);
  auto w = acct(9690); w.start_week_equity = 10000; w.high_water_mark = 10000;
  EXPECT_EQ(evaluate_mode(cfg(), w).mode, RiskMode::Locked);
  auto m = acct(9490); m.start_month_equity = 10000; m.start_week_equity = m.start_day_equity = 9490; m.high_water_mark = 9490;
  EXPECT_EQ(evaluate_mode(cfg(), m).mode, RiskMode::Locked);
}

TEST(Risk, DrawdownStateMachine) {
  auto c = cfg();
  auto a = acct(9790); a.high_water_mark = 10000;  // 2.1% dd, no daily loss
  EXPECT_EQ(evaluate_mode(c, a).mode, RiskMode::Caution);
  auto d = check_and_size(c, a, stock("SPY", 100, 96.5));
  EXPECT_TRUE(d.approved);
  EXPECT_NEAR(d.risk_budget, 9790 * 0.001, 1e-9);  // reduced risk in CAUTION
  a.equity = a.start_day_equity = a.start_week_equity = a.start_month_equity = 9590;
  EXPECT_EQ(evaluate_mode(c, a).mode, RiskMode::Recovery);
  EXPECT_FALSE(check_and_size(c, a, stock()).approved);
  a.equity = a.start_day_equity = a.start_week_equity = a.start_month_equity = 9190;
  EXPECT_EQ(evaluate_mode(c, a).mode, RiskMode::Locked);
}

TEST(Risk, PersistedLockNeverAutoUnlocks) {
  auto a = acct(); a.persisted_mode = RiskMode::Locked;
  EXPECT_EQ(evaluate_mode(cfg(), a).mode, RiskMode::Locked);
}

TEST(Risk, ConsecutiveLossesAndCooldown) {
  auto a = acct(); a.consecutive_losses = 3;
  EXPECT_EQ(evaluate_mode(cfg(), a).mode, RiskMode::Caution);
  auto b = acct(); b.last_loss_time = b.now - 600;
  EXPECT_TRUE(has(check_and_size(cfg(), b, stock()), "cooldown"));
  b.last_loss_time = b.now - 3600;
  EXPECT_TRUE(check_and_size(cfg(), b, stock()).approved);
}

TEST(Risk, PositionAndTradeCaps) {
  auto a = acct();
  for (auto s : {"AAPL", "MSFT", "TSLA"}) a.positions.push_back({s, AssetClass::Stock, 1, 100, 99, 100});
  EXPECT_TRUE(has(check_and_size(cfg(), a, stock()), "max simultaneous"));
  auto b = acct(); b.trades_today = 6;
  EXPECT_TRUE(has(check_and_size(cfg(), b, stock()), "per day"));
}

TEST(Risk, NoAveragingDownOrDuplicateSymbol) {
  auto a = acct(); a.positions.push_back({"SPY", AssetClass::Stock, 5, 100, 99, 98});
  EXPECT_TRUE(has(check_and_size(cfg(), a, stock("SPY", 98, 97)), "already open"));
  auto b = acct(); b.pending_orders.push_back({"SPY", AssetClass::Stock, 5, 100, 99, 100});
  EXPECT_TRUE(has(check_and_size(cfg(), b, stock()), "pending order"));
}

TEST(Risk, CorrelatedPositionsReduceRisk) {
  auto a = acct(); a.positions.push_back({"QQQ", AssetClass::Stock, 5, 100, 99, 100});
  auto d = check_and_size(cfg(), a, stock("NVDA", 100, 96.5));
  EXPECT_NEAR(d.risk_budget, 12.5, 1e-9);  // half budget
  EXPECT_DOUBLE_EQ(d.quantity, 3);         // floor(12.5/3.5)
}

TEST(Risk, ExposureAndBuyingPowerLimits) {
  auto a = acct(); a.positions.push_back({"AAPL", AssetClass::Stock, 28, 100, 99, 100});  // $2800 of $3000 stock cap
  auto d = check_and_size(cfg(), a, stock("MSFT", 100, 96.5));
  EXPECT_DOUBLE_EQ(d.quantity, 2);
  auto b = acct(); b.buying_power = 150;  // insufficient funds for more than 1 share
  EXPECT_DOUBLE_EQ(check_and_size(cfg(), b, stock("SPY", 100, 96.5)).quantity, 1);
  auto c = acct(); c.buying_power = 50;
  EXPECT_TRUE(has(check_and_size(cfg(), c, stock()), "below minimum"));
}

TEST(Risk, CryptoExposureCap) {
  auto a = acct();
  a.positions.push_back({"BTC-USD", AssetClass::Crypto, 0.02, 75000, 74000, 75000});  // $1500 = 15%
  Proposal p; p.symbol = "SOL-USD"; p.ac = AssetClass::Crypto; p.entry = 90; p.stop = 88; p.qty_increment = 1e-6; p.min_qty = 1e-6;
  EXPECT_FALSE(check_and_size(cfg(), a, p).approved);
}

TEST(Risk, ProhibitedOrders) {
  auto a = acct();
  Proposal s = stock(); s.side = "sell_short";
  EXPECT_TRUE(has(check_and_size(cfg(), a, s), "long"));
  EXPECT_TRUE(has(check_and_size(cfg(), a, stock("SPY", 100, 101)), "stop must be below"));
  a.operating_mode = "LIVE";
  Proposal r = stock(); r.research_only = true;
  EXPECT_TRUE(has(check_and_size(cfg(), a, r), "research-only"));
  RiskConfig c = cfg(); c.allowed_symbols = {"QQQ"};
  EXPECT_TRUE(has(check_and_size(c, acct(), stock()), "watchlist"));
}

TEST(Risk, ConfigValidation) {
  EXPECT_THROW(parse_risk_config(json{{"risk_per_trade", 2.0}}), ValidationError);
  EXPECT_THROW(parse_risk_config(json{{"caution_drawdown", 0.05}, {"recovery_drawdown", 0.04}}), ValidationError);
  EXPECT_THROW(parse_account(json{{"equity", -1}, {"buying_power", 0}, {"now", 1}}), ValidationError);
}
