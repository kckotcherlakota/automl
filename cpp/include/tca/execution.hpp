// Order-price optimisation and execution-quality filters (incl. Strategy H
// microstructure filter, used ONLY as an entry filter, never as a strategy).
#pragma once

#include "tca/common.hpp"

namespace tca {

struct Quote {
  double bid = 0, ask = 0, bid_size = 0, ask_size = 0;
  int64_t ts = 0;
};

struct ExecConfig {
  double max_spread_bps = 15;      // reject wider spreads
  double max_quote_age_sec = 10;   // stale quote => reject
  double max_move_bps = 30;        // quote moved too far from signal price => reject
  double aggressiveness = 0.25;    // limit = mid + aggressiveness * spread/2 (buy)
  double slippage_bps = 2;         // modelled extra slippage
  double min_imbalance = -0.6;     // H filter: (bid-ask size)/(sum) below this => reject
  int max_order_age_sec = 60;
  int max_retries = 1;
  int min_retry_delay_sec = 5;
};

ExecConfig parse_exec_config(const json& j);
Quote parse_quote(const json& j);

struct ExecPlan {
  bool ok = false;
  double limit_price = 0, mid = 0, spread_bps = 0, move_bps = 0, imbalance = 0, est_fill_slippage = 0;
  std::vector<std::string> reasons;
  json to_json(const ExecConfig& c) const;
};

// Round a buy limit DOWN to tick, sell limit UP to tick.
double round_to_tick(double px, double tick, bool down);

ExecPlan plan_entry(const ExecConfig& c, const Quote& q, double signal_price, double tick, int64_t now,
                    int retries_used);

}  // namespace tca
