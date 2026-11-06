// market_analyzer: latest indicators + regime + data freshness for one symbol.
#include "tca/signals.hpp"
using namespace tca;
static json num(double x) { return std::isfinite(x) ? json(x) : json(nullptr); }
static int run(const json& in, json& out) {
  auto ac = parse_asset_class(get_str(in, "asset_class"));
  auto tf = timeframe_seconds(get_str(in, "timeframe"));
  auto bars = parse_bars(in);
  int64_t now = static_cast<int64_t>(get_num_or(in, "now", 0));
  int dropped = now > 0 ? drop_incomplete(bars, tf, now) : 0;
  if (bars.empty()) throw ValidationError("no completed bars");
  Context c = build_context(bars, ac, tf);
  size_t i = bars.size() - 1;
  const Bar& b = bars[i];
  out = {{"tool", "market_analyzer"}, {"symbol", get_str_or(in, "symbol", "")}, {"asset_class", to_string(ac)},
         {"bars_used", bars.size()}, {"incomplete_bars_dropped", dropped}, {"last_bar_t", b.t},
         {"data_age_sec", now > 0 ? json(now - (b.t + tf)) : json(nullptr)},
         {"close", b.c},
         {"indicators", {{"ema9", num(c.ema9[i])}, {"ema20", num(c.ema20[i])}, {"ema21", num(c.ema21[i])},
                         {"ema50", num(c.ema50[i])}, {"ema200", num(c.ema200[i])}, {"rsi14", num(c.rsi[i])},
                         {"atr14", num(c.atr[i])}, {"adx14", num(c.adx[i])}, {"vwap", num(c.vwap[i])},
                         {"relvol20", num(c.relvol[i])}, {"prior_20_high", num(c.ph20[i])},
                         {"htf_ema20", num(c.htf_ema20[i])}, {"htf_ema50", num(c.htf_ema50[i])}}},
         {"regime", classify_regime(c, i).to_json()}};
  return kOk;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "market_analyzer", run); }
