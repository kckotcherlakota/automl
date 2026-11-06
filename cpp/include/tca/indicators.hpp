// Incremental (O(1) per update) technical indicators plus series helpers.
// Every series helper returns a vector aligned with the input bars; values
// are NaN until the indicator has enough history ("warm-up").
#pragma once

#include <cmath>
#include <deque>
#include <limits>
#include <vector>

#include "tca/common.hpp"

namespace tca {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

class Ema {
 public:
  explicit Ema(int period);
  double update(double x);  // returns NaN until `period` samples (seeded with SMA)
  bool ready() const { return n_ >= period_; }
  double value() const { return ready() ? v_ : kNaN; }
 private:
  int period_; double k_; int n_ = 0; double sum_ = 0, v_ = 0;
};

class Sma {
 public:
  explicit Sma(int period);
  double update(double x);
 private:
  int period_; std::deque<double> q_; double sum_ = 0;
};

class Rsi {  // Wilder smoothing
 public:
  explicit Rsi(int period = 14);
  double update(double close);
 private:
  int period_; int n_ = 0; double prev_ = 0, ag_ = 0, al_ = 0; bool have_prev_ = false;
};

class Atr {  // Wilder smoothing of true range
 public:
  explicit Atr(int period = 14);
  double update(const Bar& b);
 private:
  int period_; int n_ = 0; double prev_close_ = 0, atr_ = 0, sum_ = 0; bool have_prev_ = false;
};

class Adx {  // Wilder ADX with +DI/-DI
 public:
  explicit Adx(int period = 14);
  double update(const Bar& b);
  double plus_di() const { return pdi_; }
  double minus_di() const { return mdi_; }
 private:
  int period_; int n_ = 0; Bar prev_{}; bool have_prev_ = false;
  double tr_s_ = 0, pdm_s_ = 0, mdm_s_ = 0, adx_ = 0, dx_sum_ = 0; int dx_n_ = 0;
  double pdi_ = kNaN, mdi_ = kNaN;
};

// Session VWAP. Session key changes => reset. Uses typical price (h+l+c)/3.
class SessionVwap {
 public:
  double update(const Bar& b, int session_key);
 private:
  int key_ = -1; double pv_ = 0, vol_ = 0, last_ = kNaN;
};

// Rolling max/min over the previous `period` values (monotonic deque).
class RollingExtreme {
 public:
  RollingExtreme(int period, bool is_max) : period_(period), is_max_(is_max) {}
  // Returns extreme of values pushed BEFORE this call (exclusive), then pushes x.
  double prior_then_push(double x);
 private:
  int period_; bool is_max_; int64_t idx_ = 0; std::deque<std::pair<int64_t, double>> dq_;
};

// ---- series helpers ----
std::vector<double> ema_series(const std::vector<Bar>& bars, int period);
std::vector<double> sma_volume_series(const std::vector<Bar>& bars, int period);
std::vector<double> rsi_series(const std::vector<Bar>& bars, int period);
std::vector<double> atr_series(const std::vector<Bar>& bars, int period);
std::vector<double> adx_series(const std::vector<Bar>& bars, int period);
// Session VWAP: stocks reset per ET trading date; crypto per UTC date.
std::vector<double> vwap_series(const std::vector<Bar>& bars, AssetClass ac);
// Timeframe-aware: intraday => session VWAP; daily or longer => rolling 20-bar VWAP.
std::vector<double> vwap_series(const std::vector<Bar>& bars, AssetClass ac, int64_t tf_sec);
// Highest high / lowest low of the previous `period` bars (excludes current).
std::vector<double> prior_high_series(const std::vector<Bar>& bars, int period);
std::vector<double> prior_low_series(const std::vector<Bar>& bars, int period);
// Relative volume: volume / SMA(volume, period) of the previous bars.
std::vector<double> relvol_series(const std::vector<Bar>& bars, int period);
// Bollinger band width (4*stdev/sma) over `period`.
std::vector<double> bb_width_series(const std::vector<Bar>& bars, int period);

// Aggregate completed lower-timeframe bars into a higher timeframe.
// Only buckets that are COMPLETE (bucket end <= last bar end) are emitted,
// so no look-ahead leaks into the higher timeframe.
std::vector<Bar> aggregate(const std::vector<Bar>& bars, int64_t from_sec, int64_t to_sec);

}  // namespace tca
