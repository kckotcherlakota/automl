#include <gtest/gtest.h>
#include "tca/backtest.hpp"
#include "test_util.hpp"
using namespace tca;

TEST(Backtest, StopExitIncludesCostsAndNoLookAheadEntry) {
  auto bars = tt::orb_scenario(true);
  Context c = build_context(bars, AssetClass::Stock, 300);
  BacktestConfig cfg; cfg.slippage_bps = 1; cfg.spread_bps = 2; cfg.fee_bps = 0;
  cfg.params.round_trip_cost_frac = 2 * (1 + 1) / 1e4;
  auto r = run_backtest(c, cfg, 100, bars.size());
  ASSERT_GE(r.trades.size(), 1u);
  const Trade& t = r.trades.front();
  EXPECT_EQ(t.strategy, "A_ORB");
  EXPECT_EQ(t.entry_t, bars[161].t);             // fills on the bar AFTER the signal
  EXPECT_NEAR(t.entry, 100.85 * 1.0002, 1e-9);   // open + slippage + half spread
  EXPECT_EQ(t.exit_reason, "stop");
  EXPECT_NEAR(t.exit, 100.0 * 0.9998, 1e-9);
  EXPECT_LT(t.pnl, 0);
  EXPECT_GT(t.costs, 0);
  EXPECT_NEAR(t.pnl, (t.exit - t.entry) * t.qty, 1e-9);
  EXPECT_LE(-t.pnl, cfg.initial_equity * cfg.risk_per_trade * 1.05);  // loss bounded near risk budget
}

TEST(Backtest, StopAssumedFirstWhenBothHit) {
  auto bars = tt::orb_scenario(true);
  // Make the bar after entry span both stop and target.
  bars[162] = {bars[162].t, 100.6, 103.0, 99.9, 100.0, 1500};
  Context c = build_context(bars, AssetClass::Stock, 300);
  auto r = run_backtest(c, BacktestConfig{}, 100, bars.size());
  ASSERT_GE(r.trades.size(), 1u);
  EXPECT_EQ(r.trades.front().exit_reason, "stop");
}

TEST(Backtest, GapThroughStopFillsAtOpen) {
  auto bars = tt::orb_scenario(true);
  bars[162] = {bars[162].t, 99.5, 99.6, 99.4, 99.5, 1500};
  Context c = build_context(bars, AssetClass::Stock, 300);
  BacktestConfig cfg; cfg.slippage_bps = 0; cfg.spread_bps = 0;
  auto r = run_backtest(c, cfg, 100, bars.size());
  ASSERT_GE(r.trades.size(), 1u);
  EXPECT_EQ(r.trades.front().exit_reason, "stop_gap");
  EXPECT_DOUBLE_EQ(r.trades.front().exit, 99.5);
}

TEST(Backtest, FlatMarketProducesNoTradesAndMetricsAreSane) {
  auto bars = tt::orb_scenario(false);
  bars.resize(156);
  Context c = build_context(bars, AssetClass::Stock, 300);
  auto r = run_backtest(c, BacktestConfig{}, 60, bars.size());
  EXPECT_TRUE(r.trades.empty());
  json m = compute_metrics(r, AssetClass::Stock, 300);
  EXPECT_EQ(m["trades"], 0);
  EXPECT_DOUBLE_EQ(m["max_drawdown"].get<double>(), 0.0);
}

TEST(Backtest, ConfigValidation) {
  EXPECT_THROW(parse_backtest_config(json{{"risk_per_trade", 0.5}}), ValidationError);
  EXPECT_THROW(parse_backtest_config(json{{"slippage_bps", -1}}), ValidationError);
}

TEST(Backtest, DailyStockPositionsHoldAcrossBars) {
  // Flat history, then a breakout bar with heavy volume, then a slow grind higher.
  std::vector<Bar> bars;
  const int64_t t0 = tt::kNov4 - 400LL * 86400;
  for (int i = 0; i < 300; ++i) {
    double px = 100 + (i % 2 ? 0.3 : -0.3) + i * 0.01;
    bars.push_back({t0 + i * 86400LL, px, px + 0.8, px - 0.8, px, 1e6});
  }
  for (int i = 300; i < 340; ++i) {
    double px = 104 + (i - 300) * 0.4;
    bars.push_back({t0 + i * 86400LL, px - 0.2, px + 0.5, px - 0.5, px, i == 300 ? 3e6 : 1.2e6});
  }
  Context c = build_context(bars, AssetClass::Stock, 86400);
  BacktestConfig cfg; cfg.max_hold_bars = 20;
  auto r = run_backtest(c, cfg, 250, bars.size());
  for (const auto& t : r.trades) EXPECT_NE(t.exit_reason, "session_close");
}
