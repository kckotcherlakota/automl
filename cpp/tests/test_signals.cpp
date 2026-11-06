#include <gtest/gtest.h>
#include "tca/signals.hpp"
#include "test_util.hpp"
using namespace tca;

TEST(Signals, OpeningRangeBreakoutQualifies) {
  auto bars = tt::orb_scenario(false);
  Context c = build_context(bars, AssetClass::Stock, 300);
  Signal s = evaluate("A_ORB", c, bars.size() - 1, StrategyParams{});
  for (auto& f : s.failed) ADD_FAILURE() << f;
  ASSERT_TRUE(s.qualified);
  EXPECT_DOUBLE_EQ(s.stop, 100.0);              // opening-range low
  EXPECT_DOUBLE_EQ(s.entry, 100.85);
  EXPECT_NEAR(s.target, 100.85 + 2 * 0.85, 1e-9);  // 2R with zero costs
  EXPECT_NEAR(s.rr_net, 2.0, 1e-9);
}

TEST(Signals, OrbRejectedWhenCostsInvalidateRewardRisk) {
  auto bars = tt::orb_scenario(false);
  Context c = build_context(bars, AssetClass::Stock, 300);
  StrategyParams p; p.round_trip_cost_frac = 0.01;  // 1% round trip vs 0.85% risk
  EXPECT_FALSE(evaluate("A_ORB", c, bars.size() - 1, p).qualified);
}

TEST(Signals, OrbNotBeforeRangeCompletes) {
  auto bars = tt::orb_scenario(false);
  Context c = build_context(bars, AssetClass::Stock, 300);
  EXPECT_FALSE(evaluate("A_ORB", c, bars.size() - 3, StrategyParams{}).qualified);
}

TEST(Signals, NoLookAheadEvaluationIsCausal) {
  auto bars = tt::orb_scenario(true);
  Context full = build_context(bars, AssetClass::Stock, 300);
  for (size_t i = 150; i < bars.size(); i += 7) {
    std::vector<Bar> prefix(bars.begin(), bars.begin() + i + 1);
    Context part = build_context(prefix, AssetClass::Stock, 300);
    for (const auto& id : all_strategies()) {
      if (!strategy_applies(id, AssetClass::Stock)) continue;
      Signal a = evaluate(id, full, i, StrategyParams{}), b = evaluate(id, part, i, StrategyParams{});
      EXPECT_EQ(a.qualified, b.qualified) << id << " at " << i;
      EXPECT_EQ(a.failed, b.failed) << id << " at " << i;
    }
  }
}

TEST(Signals, InsufficientHistoryRejected) {
  auto bars = tt::orb_scenario(false);
  Context c = build_context(bars, AssetClass::Stock, 300);
  Signal s = evaluate("B_TREND", c, 10, StrategyParams{});
  EXPECT_FALSE(s.qualified);
  EXPECT_EQ(s.failed.front(), "insufficient indicator history");
}

TEST(Signals, StrategyApplicabilityAndResearchFlags) {
  EXPECT_FALSE(strategy_applies("E_CRYPTO_TREND", AssetClass::Stock));
  EXPECT_FALSE(strategy_applies("A_ORB", AssetClass::Crypto));
  EXPECT_TRUE(is_research_only("D_VWAP_MR"));
  EXPECT_TRUE(is_research_only("G_CRYPTO_VOLBRK"));
  EXPECT_FALSE(is_research_only("E_CRYPTO_TREND"));
}

TEST(Signals, RegimeInsufficientData) {
  auto bars = tt::orb_scenario(false);
  Context c = build_context(bars, AssetClass::Stock, 300);
  EXPECT_EQ(classify_regime(c, 5).label, "INSUFFICIENT_DATA");
  EXPECT_EQ(classify_regime(c, 150).label, "RANGE");
}

TEST(Signals, DailyStockBarsSkipIntradaySessionChecks) {
  std::vector<Bar> bars;
  double px = 100;
  for (int i = 0; i < 260; ++i) {  // steady uptrend, daily bars stamped at 00:00 UTC
    px *= 1.002;
    bars.push_back({tt::kNov4 - (260 - i) * 86400LL, px * 0.999, px * 1.01, px * 0.99, px, 1e6});
  }
  Context c = build_context(bars, AssetClass::Stock, 86400);
  Signal s = evaluate("B_TREND", c, bars.size() - 1, StrategyParams{});
  for (const auto& f : s.failed) {
    EXPECT_NE(f, "regular trading session");
    EXPECT_NE(f, "before last entry time");
  }
  EXPECT_FALSE(std::isnan(c.vwap.back()));
  EXPECT_LT(c.vwap.back(), bars.back().c);  // rolling VWAP lags a rising price
}
