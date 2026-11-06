// signal_engine: evaluate every applicable strategy on the latest COMPLETED bar,
// reject stale data, rank qualified signals.
#include <algorithm>
#include "tca/signals.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  auto ac = parse_asset_class(get_str(in, "asset_class"));
  auto tf = timeframe_seconds(get_str(in, "timeframe"));
  auto bars = parse_bars(in);
  const int64_t now = static_cast<int64_t>(get_num(in, "now"));
  const int64_t max_age = static_cast<int64_t>(get_num_or(in, "max_data_age_sec", 2.0 * tf));
  const bool include_research = in.value("include_research", false);
  StrategyParams p = parse_params(in.value("params", json::object()));
  int dropped = drop_incomplete(bars, tf, now);
  out = {{"tool", "signal_engine"}, {"symbol", get_str_or(in, "symbol", "")}, {"asset_class", to_string(ac)},
         {"incomplete_bars_dropped", dropped}};
  if (bars.empty()) throw ValidationError("no completed bars");
  const int64_t age = now - (bars.back().t + tf);
  out["data_age_sec"] = age;
  if (age > max_age) {
    out["stale"] = true; out["qualified"] = json::array(); out["rejected"] = json::array();
    out["summary"] = "REJECTED: market data is stale";
    return kRejected;
  }
  out["stale"] = false;
  Context c = build_context(bars, ac, tf);
  const size_t i = bars.size() - 1;
  std::vector<std::string> ids = in.contains("strategies") ? in["strategies"].get<std::vector<std::string>>() : all_strategies();
  std::vector<Signal> q; json rej = json::array();
  for (const auto& id : ids) {
    if (!strategy_applies(id, ac)) continue;
    Signal s = evaluate(id, c, i, p);
    if (s.research_only && !include_research) { s.qualified = false; s.failed.push_back("research-only strategy disabled"); }
    if (s.qualified) q.push_back(s); else rej.push_back(s.to_json());
  }
  std::sort(q.begin(), q.end(), [](const Signal& a, const Signal& b) { return a.score > b.score; });
  json qa = json::array();
  for (auto& s : q) qa.push_back(s.to_json());
  out["regime"] = classify_regime(c, i).to_json();
  out["signal_bar_t"] = bars.back().t;
  out["qualified"] = qa;
  out["rejected"] = rej;
  out["summary"] = q.empty() ? "NO QUALIFIED SIGNAL" : "QUALIFIED: " + q.front().strategy;
  return kOk;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "signal_engine", run); }
