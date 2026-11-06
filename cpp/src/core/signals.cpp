#include "tca/signals.hpp"

#include <algorithm>
#include <cmath>

namespace tca {

static bool bad(double x) { return !std::isfinite(x); }

Context build_context(const std::vector<Bar>& bars, AssetClass ac, int64_t tf) {
  Context c;
  c.ac = ac; c.tf = tf; c.bars = bars;
  c.ema9 = ema_series(bars, 9); c.ema20 = ema_series(bars, 20); c.ema21 = ema_series(bars, 21);
  c.ema50 = ema_series(bars, 50); c.ema200 = ema_series(bars, 200);
  c.rsi = rsi_series(bars, 14); c.atr = atr_series(bars, 14); c.adx = adx_series(bars, 14);
  c.vwap = vwap_series(bars, ac, tf); c.relvol = relvol_series(bars, 20);
  c.ph20 = prior_high_series(bars, 20); c.pl10 = prior_low_series(bars, 10);
  c.bbw = bb_width_series(bars, 20);
  c.atr_pct.resize(bars.size());
  for (size_t i = 0; i < bars.size(); ++i) c.atr_pct[i] = c.atr[i] / bars[i].c;

  const int64_t htf = tf < 3600 ? 3600 : tf * 4;
  std::vector<Bar> hb = aggregate(bars, tf, htf);
  auto he20 = ema_series(hb, 20), he50 = ema_series(hb, 50);
  c.htf_ema20.assign(bars.size(), kNaN); c.htf_ema50.assign(bars.size(), kNaN); c.htf_close.assign(bars.size(), kNaN);
  size_t j = 0; long last = -1;
  for (size_t i = 0; i < bars.size(); ++i) {
    const int64_t end_i = bars[i].t + tf;
    while (j < hb.size() && hb[j].t + htf <= end_i) { last = static_cast<long>(j); ++j; }
    if (last >= 0) { c.htf_ema20[i] = he20[last]; c.htf_ema50[i] = he50[last]; c.htf_close[i] = hb[last].c; }
  }
  return c;
}

StrategyParams parse_params(const json& j) {
  StrategyParams p;
  if (!j.is_object()) return p;
  p.min_rr = get_num_or(j, "min_rr", p.min_rr);
  p.round_trip_cost_frac = get_num_or(j, "round_trip_cost_frac", p.round_trip_cost_frac);
  p.max_cost_to_risk = get_num_or(j, "max_cost_to_risk", p.max_cost_to_risk);
  p.orb_min_relvol = get_num_or(j, "orb_min_relvol", p.orb_min_relvol);
  p.momentum_min_relvol = get_num_or(j, "momentum_min_relvol", p.momentum_min_relvol);
  p.crypto_min_relvol = get_num_or(j, "crypto_min_relvol", p.crypto_min_relvol);
  p.adx_trend_min = get_num_or(j, "adx_trend_min", p.adx_trend_min);
  p.max_stop_atr = get_num_or(j, "max_stop_atr", p.max_stop_atr);
  p.min_stop_atr = get_num_or(j, "min_stop_atr", p.min_stop_atr);
  p.crypto_stop_atr = get_num_or(j, "crypto_stop_atr", p.crypto_stop_atr);
  p.last_entry_minute_et = static_cast<int>(get_num_or(j, "last_entry_minute_et", p.last_entry_minute_et));
  if (p.min_rr <= 0 || p.round_trip_cost_frac < 0 || p.max_cost_to_risk <= 0)
    throw ValidationError("invalid strategy params");
  return p;
}

json Signal::to_json() const {
  json j{{"strategy", strategy}, {"applicable", applicable}, {"research_only", research_only},
         {"qualified", qualified}, {"passed", passed}, {"failed", failed}};
  if (qualified) {
    j["entry"] = entry; j["stop"] = stop; j["target"] = target;
    j["risk_per_unit"] = risk_per_unit; j["rr_net"] = rr_net; j["score"] = score;
  }
  return j;
}

const std::vector<std::string>& all_strategies() {
  static const std::vector<std::string> v{"A_ORB", "B_TREND", "C_MOMENTUM", "D_VWAP_MR",
                                          "E_CRYPTO_TREND", "F_CRYPTO_PULLBACK", "G_CRYPTO_VOLBRK"};
  return v;
}
bool is_research_only(const std::string& id) { return id == "D_VWAP_MR" || id == "G_CRYPTO_VOLBRK"; }
bool strategy_applies(const std::string& id, AssetClass ac) {
  if (id == "A_ORB" || id == "B_TREND" || id == "C_MOMENTUM" || id == "D_VWAP_MR") return ac == AssetClass::Stock;
  return ac == AssetClass::Crypto;
}

namespace {

struct Eval {
  Signal s;
  void check(bool ok, const std::string& what) { (ok ? s.passed : s.failed).push_back(what); }
};

bool htf_bullish(const Context& c, size_t i) {
  return !bad(c.htf_ema20[i]) && !bad(c.htf_ema50[i]) && c.htf_ema20[i] > c.htf_ema50[i] && c.htf_close[i] > c.htf_ema50[i];
}

double percentile_rank(const std::vector<double>& v, size_t i, size_t lookback) {
  size_t from = i >= lookback ? i - lookback : 0;
  int n = 0, below = 0;
  for (size_t k = from; k < i; ++k) {
    if (bad(v[k])) continue;
    ++n; if (v[k] < v[i]) ++below;
  }
  return n >= 20 ? static_cast<double>(below) / n : kNaN;
}

bool bullish_reversal(const Context& c, size_t i) {
  return i > 0 && c.bars[i].c > c.bars[i].o && c.bars[i].c > c.bars[i - 1].h;
}

// Sets stop/target with cost model and records the cost / RR checks.
void finalize(Eval& e, const Context& c, size_t i, double stop, double target_override, const StrategyParams& p,
              double min_rr) {
  const double entry = c.bars[i].c;
  e.s.entry = entry;
  e.s.stop = stop;
  const double risk = entry - stop;
  e.check(risk > 0, "stop below entry");
  if (risk <= 0) return;
  const double cost = entry * p.round_trip_cost_frac;
  e.check(cost <= p.max_cost_to_risk * risk, "costs small relative to risk");
  e.s.target = target_override > 0 ? target_override : entry + min_rr * (risk + cost) + cost;
  e.s.risk_per_unit = risk;
  e.s.rr_net = (e.s.target - entry - cost) / (risk + cost);
  e.check(e.s.rr_net >= min_rr - 1e-9, "net reward:risk >= " + std::to_string(min_rr).substr(0, 4));
}

}  // namespace

Signal evaluate(const std::string& id, const Context& c, size_t i, const StrategyParams& p) {
  Eval e;
  e.s.strategy = id;
  e.s.research_only = is_research_only(id);
  e.s.applicable = strategy_applies(id, c.ac);
  if (!e.s.applicable) { e.s.failed.push_back("strategy not applicable to asset class"); return e.s; }
  if (i >= c.bars.size()) throw ValidationError("bar index out of range");
  const Bar& b = c.bars[i];
  const double atr = c.atr[i];
  if (i < 60 || bad(atr) || bad(c.ema20[i]) || bad(c.ema50[i]) || bad(c.rsi[i])) {
    e.s.failed.push_back("insufficient indicator history");
    return e.s;
  }

  if (c.ac == AssetClass::Stock && c.tf < 86400) {  // intraday session rules only
    const int m = et_minute_of_day(b.t);
    const int wd = et_weekday(b.t);
    e.check(wd >= 1 && wd <= 5 && m >= 570 && m + c.tf / 60 <= 960, "regular trading session");
    e.check(m + c.tf / 60 <= p.last_entry_minute_et, "before last entry time");
  }

  if (id == "A_ORB") {
    e.check(c.tf == 300, "5-minute timeframe");
    const int d = et_date(b.t), m = et_minute_of_day(b.t);
    double orh = -1, orl = 1e300; int orn = 0;
    size_t k = i;
    while (true) {
      const Bar& x = c.bars[k];
      if (et_date(x.t) != d) break;
      int xm = et_minute_of_day(x.t);
      if (xm >= 570 && xm < 585) { orh = std::max(orh, x.h); orl = std::min(orl, x.l); ++orn; }
      if (k == 0) break;
      --k;
    }
    e.check(m >= 585, "after opening range (9:45 ET)");
    e.check(orn == 3, "complete 9:30-9:45 opening range");
    if (orn != 3) return e.s;
    e.check(b.c > orh, "close above opening-range high");
    e.check(!bad(c.vwap[i]) && b.c > c.vwap[i], "close above VWAP");
    e.check(!bad(c.ema9[i]) && !bad(c.ema21[i]) && c.ema9[i] > c.ema21[i], "EMA9 > EMA21");
    e.check(!bad(c.relvol[i]) && c.relvol[i] >= p.orb_min_relvol, "relative volume >= threshold");
    const bool fresh = c.bars[i - 1].c <= orh;
    bool retest = false;
    if (!fresh && b.l <= orh * 1.002) {
      for (size_t q = i - 1; q > 0 && et_date(c.bars[q].t) == d; --q)
        if (c.bars[q].c > orh) { retest = true; break; }
    }
    e.check(fresh || retest, "fresh breakout or confirmed retest");
    double stop = orl;
    const double risk = b.c - stop;
    e.check(risk <= p.max_stop_atr * atr, "structure stop within ATR limit");
    if (risk < p.min_stop_atr * atr) stop = b.c - p.min_stop_atr * atr;
    finalize(e, c, i, stop, -1, p, p.min_rr);
    e.s.score = (retest ? 0.5 : 0.0) + std::min(c.relvol[i], 3.0) * 0.5;
  } else if (id == "B_TREND") {
    e.check(htf_bullish(c, i), "higher-timeframe trend bullish");
    e.check(c.ema20[i] > c.ema50[i], "EMA20 > EMA50");
    e.check(!bad(c.vwap[i]) && b.c > c.vwap[i], "close above VWAP");
    e.check(!bad(c.adx[i]) && c.adx[i] > p.adx_trend_min, "ADX above trend threshold");
    double lo = 1e300; bool touched = false;
    for (size_t k = i - 5; k < i; ++k) { lo = std::min(lo, c.bars[k].l); if (c.bars[k].l <= c.ema20[k]) touched = true; }
    e.check(touched, "pullback touched EMA20");
    e.check(b.l > lo, "pullback stabilised (higher low)");
    e.check(bullish_reversal(c, i), "bullish reversal bar");
    const double stop = lo - 0.1 * atr;
    e.check(b.c - stop <= p.max_stop_atr * atr, "stop within ATR limit");
    finalize(e, c, i, stop, -1, p, p.min_rr);
    e.s.score = (bad(c.adx[i]) ? 0 : c.adx[i] / 50.0);
  } else if (id == "C_MOMENTUM") {
    e.check(!bad(c.relvol[i]) && c.relvol[i] >= p.momentum_min_relvol, "strong relative volume");
    e.check(!bad(c.ph20[i]) && b.c > c.ph20[i], "close above 20-bar high");
    e.check(htf_bullish(c, i), "higher-timeframe trend bullish");
    const double pr = percentile_rank(c.atr_pct, i, 200);
    e.check(!bad(pr) && pr >= 0.2 && pr <= 0.9, "healthy volatility (ATR% 20-90th pct)");
    e.check(b.c - b.o <= 1.5 * atr && b.c - c.ema20[i] <= 2.0 * atr, "not an extended candle");
    finalize(e, c, i, b.c - 1.5 * atr, -1, p, p.min_rr);
    e.s.score = std::min(bad(c.relvol[i]) ? 0 : c.relvol[i], 4.0) * 0.5;
  } else if (id == "D_VWAP_MR") {
    e.check(!bad(c.adx[i]) && c.adx[i] < p.adx_trend_min, "low trend strength (ADX)");
    const double dev = std::max(c.vwap[i - 1] - c.bars[i - 1].l, c.vwap[i] - b.l);
    e.check(!bad(dev) && dev >= 1.5 * atr, "price stretched >= 1.5 ATR below VWAP");
    e.check(bullish_reversal(c, i), "bullish reversal bar");
    e.check(b.c < c.vwap[i], "still below VWAP (room to revert)");
    finalize(e, c, i, std::min(b.l, c.bars[i - 1].l) - 0.1 * atr, c.vwap[i], p, 1.0);
  } else if (id == "E_CRYPTO_TREND") {
    e.check(!bad(c.htf_ema20[i]) && c.htf_ema20[i] > c.htf_ema50[i], "1h EMA20 > EMA50");
    e.check(c.ema20[i] > c.ema50[i], "15m EMA20 > EMA50");
    e.check(c.rsi[i] >= 50 && c.rsi[i] <= 70, "RSI between 50 and 70");
    e.check(!bad(c.ph20[i]) && b.c > c.ph20[i], "close above previous 20 candles");
    e.check(!bad(c.relvol[i]) && c.relvol[i] >= p.crypto_min_relvol, "relative volume >= threshold");
    finalize(e, c, i, b.c - p.crypto_stop_atr * atr, -1, p, p.min_rr);
    e.s.score = std::min(bad(c.relvol[i]) ? 0 : c.relvol[i], 4.0) * 0.5;
  } else if (id == "F_CRYPTO_PULLBACK") {
    e.check(htf_bullish(c, i), "1h trend bullish");
    e.check(c.ema20[i] > c.ema50[i], "15m EMA20 > EMA50");
    double lo = 1e300, min_rsi = 100; bool touched = false;
    for (size_t k = i - 6; k < i; ++k) {
      lo = std::min(lo, c.bars[k].l); min_rsi = std::min(min_rsi, c.rsi[k]);
      if (c.bars[k].l <= c.ema20[k]) touched = true;
    }
    e.check(touched, "controlled pullback to EMA20");
    e.check(b.c > c.ema50[i], "pullback held above EMA50");
    e.check(min_rsi < 45, "RSI reset below 45");
    e.check(c.rsi[i] > c.rsi[i - 1], "selling momentum declining (RSI rising)");
    e.check(bullish_reversal(c, i), "bullish reversal bar");
    const double stop = lo - 0.25 * atr;
    e.check(b.c - stop <= p.max_stop_atr * atr, "stop within ATR limit");
    finalize(e, c, i, stop, -1, p, p.min_rr);
    e.s.score = 1.0;
  } else if (id == "G_CRYPTO_VOLBRK") {
    const double pr = percentile_rank(c.bbw, i - 1, 100);
    e.check(!bad(pr) && pr <= 0.2, "volatility compression (BB width bottom 20%)");
    e.check(!bad(c.ph20[i]) && b.c > c.ph20[i], "close above 20-bar range");
    e.check(!bad(c.relvol[i]) && c.relvol[i] >= p.crypto_min_relvol, "volume expansion");
    e.check(htf_bullish(c, i), "1h trend bullish");
    finalize(e, c, i, b.c - 1.5 * atr, -1, p, p.min_rr);
  } else {
    throw ValidationError("unknown strategy: " + id);
  }
  e.s.qualified = e.s.failed.empty();
  if (e.s.qualified) e.s.score += e.s.rr_net;
  return e.s;
}

json Regime::to_json() const {
  return json{{"label", label}, {"adx", std::isfinite(adx) ? json(adx) : json(nullptr)},
              {"atr_pct", std::isfinite(atr_pct) ? json(atr_pct) : json(nullptr)},
              {"atr_pct_percentile", std::isfinite(atr_pct_percentile) ? json(atr_pct_percentile) : json(nullptr)},
              {"ema50_slope_10", std::isfinite(ema_slope) ? json(ema_slope) : json(nullptr)}};
}

Regime classify_regime(const Context& c, size_t i) {
  Regime r;
  if (i >= c.bars.size() || i < 60 || bad(c.ema50[i]) || bad(c.adx[i]) || bad(c.ema50[i - 10])) {
    r.label = "INSUFFICIENT_DATA";
    return r;
  }
  r.adx = c.adx[i];
  r.atr_pct = c.atr_pct[i];
  r.atr_pct_percentile = percentile_rank(c.atr_pct, i, 200);
  r.ema_slope = (c.ema50[i] - c.ema50[i - 10]) / c.ema50[i - 10];
  if (!bad(r.atr_pct_percentile) && r.atr_pct_percentile >= 0.95) r.label = "HIGH_VOLATILITY";
  else if (r.adx >= 20 && c.ema20[i] > c.ema50[i] && r.ema_slope > 0) r.label = "TRENDING_UP";
  else if (r.adx >= 20 && c.ema20[i] < c.ema50[i] && r.ema_slope < 0) r.label = "TRENDING_DOWN";
  else r.label = "RANGE";
  return r;
}

}  // namespace tca
