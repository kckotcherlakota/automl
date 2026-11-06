// market_regime: classify trending / range / high-volatility for the latest completed bar.
#include "tca/signals.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  auto ac = parse_asset_class(get_str(in, "asset_class"));
  auto tf = timeframe_seconds(get_str(in, "timeframe"));
  auto bars = parse_bars(in);
  int64_t now = static_cast<int64_t>(get_num_or(in, "now", 0));
  if (now > 0) drop_incomplete(bars, tf, now);
  if (bars.empty()) throw ValidationError("no completed bars");
  Context c = build_context(bars, ac, tf);
  out = {{"tool", "market_regime"}, {"symbol", get_str_or(in, "symbol", "")},
         {"regime", classify_regime(c, bars.size() - 1).to_json()},
         {"liquidity_note", "spread/liquidity deterioration is evaluated by order_optimizer on live quotes"}};
  return kOk;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "market_regime", run); }
