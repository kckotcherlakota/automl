// backtester: per-strategy + combined research report with OOS splits,
// rolling windows, cost sensitivity and acceptance verdicts.
#include "tca/backtest.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  auto ac = parse_asset_class(get_str(in, "asset_class"));
  auto tf = timeframe_seconds(get_str(in, "timeframe"));
  auto bars = parse_bars(in);
  if (bars.size() < 300) throw ValidationError("need at least 300 bars for a backtest");
  BacktestConfig cfg = parse_backtest_config(in.value("config", json::object()));
  Context c = build_context(bars, ac, tf);
  out = research_report(c, cfg, static_cast<int>(get_num_or(in, "folds", 4)));
  out["_meta"]["symbol"] = get_str_or(in, "symbol", "");
  out["_meta"]["tool"] = "backtester";
  return kOk;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "backtester", run); }
