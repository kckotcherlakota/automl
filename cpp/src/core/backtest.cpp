#include "tca/backtest.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>

namespace tca {

BacktestConfig parse_backtest_config(const json& j) {
  BacktestConfig c;
  if (!j.is_object()) return c;
  c.initial_equity = get_num_or(j, "initial_equity", c.initial_equity);
  c.risk_per_trade = get_num_or(j, "risk_per_trade", c.risk_per_trade);
  c.max_position_notional = get_num_or(j, "max_position_notional", c.max_position_notional);
  c.fee_bps = get_num_or(j, "fee_bps", c.fee_bps);
  c.slippage_bps = get_num_or(j, "slippage_bps", c.slippage_bps);
  c.spread_bps = get_num_or(j, "spread_bps", c.spread_bps);
  c.max_hold_bars = static_cast<int>(get_num_or(j, "max_hold_bars", c.max_hold_bars));
  c.cooldown_bars_after_loss = static_cast<int>(get_num_or(j, "cooldown_bars_after_loss", 0));
  c.include_research = j.value("include_research", false);
  if (j.contains("strategies")) c.strategies = j["strategies"].get<std::vector<std::string>>();
  c.params = parse_params(j.value("params", json::object()));
  if (!(j.contains("params") && j["params"].contains("round_trip_cost_frac")))
    c.params.round_trip_cost_frac = 2 * (c.fee_bps + c.slippage_bps + c.spread_bps / 2) / 1e4;
  if (c.initial_equity <= 0 || c.risk_per_trade <= 0 || c.risk_per_trade > 0.05)
    throw ValidationError("initial_equity must be > 0 and risk_per_trade in (0, 0.05]");
  if (c.fee_bps < 0 || c.slippage_bps < 0 || c.spread_bps < 0) throw ValidationError("costs must be >= 0");
  return c;
}

namespace {
struct Open {
  bool active = false;
  Trade t;
  double raw_entry = 0, entry_fee = 0, risk_per_unit = 0, hi = 0, lo = 0;
  size_t entry_i = 0;
};
}  // namespace

BacktestResult run_backtest(const Context& c, const BacktestConfig& cfg, size_t from, size_t to) {
  BacktestResult r;
  r.initial_equity = cfg.initial_equity;
  const auto& bars = c.bars;
  to = std::min(to, bars.size());
  std::vector<std::string> strats;
  for (const auto& s : (cfg.strategies.empty() ? all_strategies() : cfg.strategies)) {
    if (!strategy_applies(s, c.ac)) continue;
    if (is_research_only(s) && !cfg.include_research) continue;
    strats.push_back(s);
  }
  const double side = (cfg.slippage_bps + cfg.spread_bps / 2) / 1e4, fee = cfg.fee_bps / 1e4;
  double cash_equity = cfg.initial_equity;
  Open pos;
  size_t cooldown_until = 0;

  auto close_pos = [&](size_t i, double raw_px, const std::string& why) {
    const double fill = raw_px * (1 - side);
    const double exit_fee = fill * pos.t.qty * fee;
    pos.t.exit = fill; pos.t.exit_t = bars[i].t; pos.t.exit_reason = why;
    pos.t.gross_pnl = (raw_px - pos.raw_entry) * pos.t.qty;
    pos.t.pnl = (fill - pos.t.entry) * pos.t.qty - pos.entry_fee - exit_fee;
    pos.t.costs = pos.t.gross_pnl - pos.t.pnl;
    pos.t.hold_bars = static_cast<int>(i - pos.entry_i + 1);
    pos.t.r_multiple = pos.t.pnl / (pos.risk_per_unit * pos.t.qty);
    pos.t.mfe = (pos.hi - pos.t.entry) / pos.risk_per_unit;
    pos.t.mae = (pos.lo - pos.t.entry) / pos.risk_per_unit;
    cash_equity += pos.t.pnl;
    if (pos.t.pnl < 0) cooldown_until = i + 1 + cfg.cooldown_bars_after_loss;
    r.trades.push_back(pos.t);
    pos.active = false;
  };

  for (size_t i = from; i < to; ++i) {
    const Bar& b = bars[i];
    if (pos.active && i >= pos.entry_i) {
      pos.hi = std::max(pos.hi, b.h); pos.lo = std::min(pos.lo, b.l);
      if (b.o <= pos.t.stop) close_pos(i, b.o, "stop_gap");
      else if (b.l <= pos.t.stop) close_pos(i, pos.t.stop, "stop");
      else if (b.o >= pos.t.target) close_pos(i, b.o, "target_gap");
      else if (b.h >= pos.t.target) close_pos(i, pos.t.target, "target");
      else {
        bool session_end = false;
        if (c.ac == AssetClass::Stock) {
          const int m = et_minute_of_day(b.t);
          session_end = m + c.tf / 60 >= 960 || i + 1 >= to || et_date(bars[i + 1].t) != et_date(b.t);
        } else {
          session_end = static_cast<int>(i - pos.entry_i + 1) >= cfg.max_hold_bars;
        }
        if (session_end) close_pos(i, b.c, c.ac == AssetClass::Stock ? "session_close" : "time_exit");
        else if (i + 1 >= to) close_pos(i, b.c, "end_of_data");
      }
    }
    if (!pos.active && i + 1 < to && i + 1 >= cooldown_until) {
      if (c.ac == AssetClass::Stock && et_date(bars[i + 1].t) != et_date(b.t)) goto mark;
      {
        Signal best; best.score = -1e300; bool any = false;
        for (const auto& s : strats) {
          Signal sg = evaluate(s, c, i, cfg.params);
          if (sg.qualified && sg.score > best.score) { best = sg; any = true; }
        }
        if (any) {
          ++r.signals;
          const Bar& nb = bars[i + 1];
          const double fill = nb.o * (1 + side);
          if (fill <= best.stop || nb.o >= best.target) { ++r.skipped_entries; goto mark; }
          const double rpu = (fill - best.stop) + best.stop * side + fill * fee * 2;
          double qty = std::min(cash_equity * cfg.risk_per_trade / rpu, cash_equity * cfg.max_position_notional / fill);
          if (qty <= 0) { ++r.skipped_entries; goto mark; }
          pos = Open{};
          pos.active = true; pos.entry_i = i + 1; pos.raw_entry = nb.o; pos.risk_per_unit = fill - best.stop;
          pos.t.strategy = best.strategy; pos.t.entry_t = nb.t; pos.t.entry = fill; pos.t.qty = qty;
          pos.t.stop = best.stop; pos.t.target = best.target; pos.t.entry_hour_et = et_minute_of_day(nb.t) / 60;
          pos.t.regime = classify_regime(c, i).label;
          pos.entry_fee = fill * qty * fee;
          pos.hi = nb.o; pos.lo = nb.o;
        }
      }
    }
  mark:
    double mtm = cash_equity;
    if (pos.active && i >= pos.entry_i) mtm += (b.c * (1 - side) - pos.t.entry) * pos.t.qty - pos.entry_fee;
    r.equity.emplace_back(b.t + c.tf, mtm);
  }
  return r;
}

static double stdev(const std::vector<double>& v) {
  if (v.size() < 2) return 0;
  double m = std::accumulate(v.begin(), v.end(), 0.0) / v.size(), s = 0;
  for (double x : v) s += (x - m) * (x - m);
  return std::sqrt(s / (v.size() - 1));
}

json compute_metrics(const BacktestResult& r, AssetClass ac, int64_t tf, unsigned seed) {
  json m;
  const auto& T = r.trades;
  double net = 0, gross = 0, wins = 0, gw = 0, gl = 0, costs = 0, hold = 0, rsum = 0;
  int streak = 0, longest = 0;
  for (const auto& t : T) {
    net += t.pnl; gross += t.gross_pnl; costs += t.costs; hold += t.hold_bars; rsum += t.r_multiple;
    if (t.pnl > 0) { ++wins; gw += t.pnl; streak = 0; } else { gl -= t.pnl; longest = std::max(longest, ++streak); }
  }
  const size_t n = T.size();
  m["trades"] = n;
  m["signals"] = r.signals;
  m["fill_rate"] = r.signals ? static_cast<double>(n) / r.signals : 0.0;
  m["net_pnl"] = net;
  m["gross_pnl"] = gross;
  m["total_costs"] = costs;
  m["total_return"] = net / r.initial_equity;
  m["win_rate"] = n ? wins / n : 0.0;
  m["profit_factor"] = gl > 0 ? json(gw / gl) : (gw > 0 ? json("inf") : json(nullptr));
  m["expectancy"] = n ? net / n : 0.0;
  m["avg_r"] = n ? rsum / n : 0.0;
  m["avg_cost_per_trade"] = n ? costs / n : 0.0;
  m["avg_hold_minutes"] = n ? hold / n * tf / 60.0 : 0.0;
  m["longest_losing_streak"] = longest;

  // Drawdown from equity curve.
  double peak = r.initial_equity, mdd = 0; size_t in_dd = 0, cur_len = 0, max_len = 0;
  std::map<int64_t, double> daily;
  for (const auto& [t, e] : r.equity) {
    if (e >= peak) { peak = e; cur_len = 0; } else { ++in_dd; max_len = std::max(max_len, ++cur_len); }
    mdd = std::max(mdd, (peak - e) / peak);
    daily[t / 86400] = e;
  }
  m["max_drawdown"] = mdd;
  m["time_in_drawdown_frac"] = r.equity.empty() ? 0.0 : static_cast<double>(in_dd) / r.equity.size();
  m["longest_drawdown_minutes"] = static_cast<double>(max_len) * tf / 60.0;
  std::vector<double> rets; double prev = r.initial_equity;
  for (const auto& [d, e] : daily) { rets.push_back(e / prev - 1); prev = e; }
  const double ann = std::sqrt(ac == AssetClass::Stock ? 252.0 : 365.0);
  double mean = rets.empty() ? 0 : std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
  double sd = stdev(rets), dsd = 0;
  { std::vector<double> neg; for (double x : rets) neg.push_back(std::min(0.0, x));
    double s = 0; for (double x : neg) s += x * x; dsd = rets.size() > 1 ? std::sqrt(s / (rets.size() - 1)) : 0; }
  m["sharpe"] = sd > 0 ? json(mean / sd * ann) : json(nullptr);
  m["sortino"] = dsd > 0 ? json(mean / dsd * ann) : json(nullptr);

  // Bootstrap CI of expectancy.
  if (n >= 5) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> pick(0, n - 1);
    std::vector<double> means;
    for (int b = 0; b < 2000; ++b) {
      double s = 0; for (size_t k = 0; k < n; ++k) s += T[pick(rng)].pnl;
      means.push_back(s / n);
    }
    std::sort(means.begin(), means.end());
    m["expectancy_ci90"] = {means[100], means[1899]};
    double pos = 0; for (double x : means) pos += x > 0;
    m["bootstrap_prob_positive_expectancy"] = pos / means.size();
  }
  json by_strat = json::object(), by_hour = json::object(), by_regime = json::object();
  for (const auto& t : T) {
    const std::pair<json*, std::string> keys[] = {
        {&by_strat, t.strategy}, {&by_hour, std::to_string(t.entry_hour_et)}, {&by_regime, t.regime}};
    for (const auto& [obj, key] : keys) {
      json& o = (*obj)[key];
      if (!o.is_object()) o = json::object();
      o["trades"] = o.value("trades", 0) + 1;
      o["net_pnl"] = o.value("net_pnl", 0.0) + t.pnl;
      o["wins"] = o.value("wins", 0) + (t.pnl > 0 ? 1 : 0);
    }
  }
  m["by_strategy"] = by_strat;
  m["by_hour_et"] = by_hour;
  m["by_regime"] = by_regime;
  json exits = json::object();
  for (const auto& t : T) exits[t.exit_reason] = exits.value(t.exit_reason, 0) + 1;
  m["exit_reasons"] = exits;
  return m;
}

static json acceptance(const json& full, const json& test, const json& folds) {
  json a; std::vector<std::string> fails;
  auto pf = [](const json& m) { return m["profit_factor"].is_number() ? m["profit_factor"].get<double>()
                                       : (m["profit_factor"].is_string() ? 1e9 : 0.0); };
  int pos_folds = 0, n_folds = 0;
  for (const auto& f : folds) { ++n_folds; if (f["metrics"]["net_pnl"].get<double>() > 0) ++pos_folds; }
  if (test["expectancy"].get<double>() <= 0) fails.push_back("out-of-sample expectancy not positive after costs");
  if (pf(full) <= 1.2) fails.push_back("profit factor <= 1.20");
  if (full["trades"].get<size_t>() < 30) fails.push_back("fewer than 30 trades (insufficient sample)");
  if (n_folds > 0 && pos_folds * 2 < n_folds) fails.push_back("unstable across rolling windows");
  if (full["max_drawdown"].get<double>() > 0.10) fails.push_back("max drawdown > 10%");
  a["accepted"] = fails.empty();
  a["failures"] = fails;
  a["positive_folds"] = pos_folds;
  a["folds"] = n_folds;
  a["note"] = "research filter only; acceptance is not a guarantee of live profitability";
  return a;
}

json research_report(const Context& c, const BacktestConfig& cfg, int folds) {
  json rep;
  const size_t N = c.bars.size();
  const size_t warm = std::min<size_t>(N, 200);
  std::vector<std::vector<std::string>> sets;
  std::vector<std::string> names;
  for (const auto& s : (cfg.strategies.empty() ? all_strategies() : cfg.strategies)) {
    if (!strategy_applies(s, c.ac) || (is_research_only(s) && !cfg.include_research)) continue;
    sets.push_back({s}); names.push_back(s);
  }
  if (sets.size() > 1) {
    std::vector<std::string> all;
    for (auto& s : sets) all.push_back(s[0]);
    sets.push_back(all); names.push_back("COMBINED");
  }
  const size_t span = N > warm ? N - warm : 0;
  const size_t tr_end = warm + span * 6 / 10, va_end = warm + span * 8 / 10;
  for (size_t k = 0; k < sets.size(); ++k) {
    BacktestConfig cc = cfg; cc.strategies = sets[k];
    json s;
    auto full = run_backtest(c, cc, warm, N);
    s["full"] = compute_metrics(full, c.ac, c.tf);
    s["train"] = compute_metrics(run_backtest(c, cc, warm, tr_end), c.ac, c.tf);
    s["validation"] = compute_metrics(run_backtest(c, cc, tr_end, va_end), c.ac, c.tf);
    s["test_out_of_sample"] = compute_metrics(run_backtest(c, cc, va_end, N), c.ac, c.tf);
    json fl = json::array();
    for (int f = 0; f < folds && span > 0; ++f) {
      size_t a = warm + span * f / folds, b = warm + span * (f + 1) / folds;
      auto m = compute_metrics(run_backtest(c, cc, a, b), c.ac, c.tf);
      json fm = {{"trades", m["trades"]}, {"net_pnl", m["net_pnl"]}, {"win_rate", m["win_rate"]},
                 {"max_drawdown", m["max_drawdown"]}};
      fl.push_back(json{{"from_t", c.bars[a].t}, {"to_t", c.bars[b - 1].t}, {"metrics", fm}});
    }
    s["rolling_windows"] = fl;
    json sens = json::array();
    for (double mult : {1.0, 2.0, 3.0}) {
      BacktestConfig sc = cc; sc.slippage_bps *= mult; sc.spread_bps *= mult;
      auto m = compute_metrics(run_backtest(c, sc, warm, N), c.ac, c.tf);
      sens.push_back({{"cost_multiplier", mult}, {"net_pnl", m["net_pnl"]}, {"profit_factor", m["profit_factor"]},
                      {"trades", m["trades"]}});
    }
    s["cost_sensitivity"] = sens;
    s["acceptance"] = acceptance(s["full"], s["test_out_of_sample"], fl);
    json tl = json::array();
    for (size_t q = full.trades.size() > 20 ? full.trades.size() - 20 : 0; q < full.trades.size(); ++q) {
      const auto& t = full.trades[q];
      tl.push_back({{"strategy", t.strategy}, {"entry_t", t.entry_t}, {"exit_t", t.exit_t}, {"entry", t.entry},
                    {"exit", t.exit}, {"pnl", t.pnl}, {"r", t.r_multiple}, {"exit_reason", t.exit_reason}});
    }
    s["last_trades"] = tl;
    rep[names[k]] = s;
  }
  rep["_meta"] = {{"bars", N}, {"warmup_bars", warm}, {"first_t", N ? c.bars.front().t : 0},
                  {"last_t", N ? c.bars.back().t : 0}, {"timeframe_sec", c.tf}, {"asset_class", to_string(c.ac)},
                  {"split", "chronological 60/20/20 after warm-up; fixed (untuned) parameters"},
                  {"cost_model_bps_per_side", (cfg.fee_bps + cfg.slippage_bps + cfg.spread_bps / 2)}};
  return rep;
}

}  // namespace tca
