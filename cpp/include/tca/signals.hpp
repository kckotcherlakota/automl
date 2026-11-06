// Strategy signal evaluation and market-regime classification.
// All evaluation at bar index i uses only data from bars [0..i] (completed
// bars) and higher-timeframe bars that had fully closed by the end of bar i.
#pragma once

#include <string>
#include <vector>

#include "tca/common.hpp"
#include "tca/indicators.hpp"

namespace tca {

struct Context {
  AssetClass ac = AssetClass::Stock;
  int64_t tf = 300;
  std::vector<Bar> bars;
  std::vector<double> ema9, ema20, ema21, ema50, ema200, rsi, atr, adx, vwap, relvol, ph20, pl10, bbw, atr_pct;
  // Higher timeframe (1h) values mapped onto each base bar, causal.
  std::vector<double> htf_ema20, htf_ema50, htf_close;
};

Context build_context(const std::vector<Bar>& bars, AssetClass ac, int64_t tf_sec);

struct StrategyParams {
  double min_rr = 2.0;               // target R multiple, net of round-trip costs
  double round_trip_cost_frac = 0.0; // fees + slippage + spread, fraction of price (round trip)
  double max_cost_to_risk = 0.25;    // reject if costs exceed this fraction of risk
  double orb_min_relvol = 1.5;
  double momentum_min_relvol = 2.0;
  double crypto_min_relvol = 1.5;
  double adx_trend_min = 20.0;
  double max_stop_atr = 2.5;         // structure stop farther than this many ATR => reject
  double min_stop_atr = 0.5;         // structure stop tighter than this is widened
  double crypto_stop_atr = 2.0;
  int last_entry_minute_et = 15 * 60 + 30;  // no new stock entries after 15:30 ET
};

StrategyParams parse_params(const json& j);

struct Signal {
  std::string strategy;
  bool applicable = true;
  bool research_only = false;
  bool qualified = false;
  double entry = 0, stop = 0, target = 0, risk_per_unit = 0, rr_net = 0, score = 0;
  std::vector<std::string> passed, failed;
  json to_json() const;
};

const std::vector<std::string>& all_strategies();
bool is_research_only(const std::string& id);
bool strategy_applies(const std::string& id, AssetClass ac);

Signal evaluate(const std::string& id, const Context& c, size_t i, const StrategyParams& p);

// Regime: TRENDING_UP, TRENDING_DOWN, RANGE, HIGH_VOLATILITY, INSUFFICIENT_DATA.
struct Regime {
  std::string label;
  double adx = kNaN, atr_pct = kNaN, atr_pct_percentile = kNaN, ema_slope = kNaN;
  json to_json() const;
};
Regime classify_regime(const Context& c, size_t i);

}  // namespace tca
