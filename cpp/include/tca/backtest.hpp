// Event-driven bar-replay backtester. Signals are evaluated on the CLOSE of
// bar i using only bars [0..i]; entries fill at the OPEN of bar i+1 with
// costs. If stop and target are both touched in one bar, the stop is assumed
// to fill first (conservative). Gaps through a stop fill at the open.
#pragma once

#include <string>
#include <vector>

#include "tca/signals.hpp"

namespace tca {

struct BacktestConfig {
  double initial_equity = 10000;
  double risk_per_trade = 0.0025;
  double max_position_notional = 0.10;
  double fee_bps = 0;        // per side
  double slippage_bps = 2;   // per side
  double spread_bps = 2;     // full spread; half paid per side
  int max_hold_bars = 96;    // crypto time exit
  int cooldown_bars_after_loss = 0;
  bool include_research = false;
  std::vector<std::string> strategies;  // empty = all applicable
  StrategyParams params;
};

BacktestConfig parse_backtest_config(const json& j);

struct Trade {
  std::string strategy, exit_reason, regime;
  int64_t entry_t = 0, exit_t = 0;
  double entry = 0, exit = 0, qty = 0, stop = 0, target = 0, pnl = 0, gross_pnl = 0, costs = 0, r_multiple = 0;
  int hold_bars = 0, entry_hour_et = 0;
  double mfe = 0, mae = 0;  // in R
};

struct BacktestResult {
  std::vector<Trade> trades;
  std::vector<std::pair<int64_t, double>> equity;  // bar close time, equity
  int signals = 0, skipped_entries = 0;
  double initial_equity = 0;
};

BacktestResult run_backtest(const Context& c, const BacktestConfig& cfg, size_t from, size_t to);

json compute_metrics(const BacktestResult& r, AssetClass ac, int64_t tf, unsigned seed = 42);

// Full research report: per-strategy + combined, chronological splits,
// rolling walk-forward windows, slippage sensitivity, acceptance verdicts.
json research_report(const Context& c, const BacktestConfig& cfg, int folds);

}  // namespace tca
