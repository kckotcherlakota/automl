#include "tca/risk.hpp"

#include <algorithm>
#include <cmath>

namespace tca {

static double frac_field(const json& j, const char* k, double d) {
  double v = get_num_or(j, k, d);
  if (v < 0 || v > 1) throw ValidationError(std::string(k) + " must be a fraction in [0,1]");
  return v;
}

RiskConfig parse_risk_config(const json& j) {
  RiskConfig c;
  if (!j.is_object()) return c;
  c.risk_per_trade = frac_field(j, "risk_per_trade", c.risk_per_trade);
  c.caution_risk_per_trade = frac_field(j, "caution_risk_per_trade", c.caution_risk_per_trade);
  c.max_trade_risk = frac_field(j, "max_trade_risk", c.max_trade_risk);
  c.max_position_notional = frac_field(j, "max_position_notional", c.max_position_notional);
  c.max_stock_exposure = frac_field(j, "max_stock_exposure", c.max_stock_exposure);
  c.max_crypto_exposure = frac_field(j, "max_crypto_exposure", c.max_crypto_exposure);
  c.max_total_exposure = frac_field(j, "max_total_exposure", c.max_total_exposure);
  c.min_uncommitted = frac_field(j, "min_uncommitted", c.min_uncommitted);
  c.max_open_positions = static_cast<int>(get_num_or(j, "max_open_positions", c.max_open_positions));
  c.max_trades_per_day = static_cast<int>(get_num_or(j, "max_trades_per_day", c.max_trades_per_day));
  c.daily_loss_limit = frac_field(j, "daily_loss_limit", c.daily_loss_limit);
  c.weekly_loss_limit = frac_field(j, "weekly_loss_limit", c.weekly_loss_limit);
  c.monthly_loss_limit = frac_field(j, "monthly_loss_limit", c.monthly_loss_limit);
  c.caution_drawdown = frac_field(j, "caution_drawdown", c.caution_drawdown);
  c.recovery_drawdown = frac_field(j, "recovery_drawdown", c.recovery_drawdown);
  c.lock_drawdown = frac_field(j, "lock_drawdown", c.lock_drawdown);
  c.pause_after_losses = static_cast<int>(get_num_or(j, "pause_after_losses", c.pause_after_losses));
  c.loss_cooldown_sec = static_cast<int>(get_num_or(j, "loss_cooldown_sec", c.loss_cooldown_sec));
  c.correlated_risk_scale = frac_field(j, "correlated_risk_scale", c.correlated_risk_scale);
  if (j.contains("allowed_symbols")) c.allowed_symbols = j["allowed_symbols"].get<std::vector<std::string>>();
  if (j.contains("correlation_groups"))
    c.correlation_groups = j["correlation_groups"].get<std::map<std::string, std::vector<std::string>>>();
  if (c.max_trade_risk < c.risk_per_trade) throw ValidationError("max_trade_risk must be >= risk_per_trade");
  if (!(c.caution_drawdown < c.recovery_drawdown && c.recovery_drawdown < c.lock_drawdown))
    throw ValidationError("drawdown thresholds must satisfy caution < recovery < lock");
  if (c.max_open_positions < 1 || c.max_trades_per_day < 1) throw ValidationError("position/trade caps must be >= 1");
  return c;
}

std::string to_string(RiskMode m) {
  switch (m) {
    case RiskMode::Normal: return "NORMAL";
    case RiskMode::Caution: return "CAUTION";
    case RiskMode::Recovery: return "RECOVERY";
    case RiskMode::Locked: return "LOCKED";
  }
  return "LOCKED";
}

RiskMode parse_risk_mode(const std::string& s) {
  if (s == "NORMAL") return RiskMode::Normal;
  if (s == "CAUTION") return RiskMode::Caution;
  if (s == "RECOVERY") return RiskMode::Recovery;
  if (s == "LOCKED") return RiskMode::Locked;
  throw ValidationError("unknown risk mode: " + s);
}

static Position parse_position(const json& j) {
  Position p;
  p.symbol = get_str(j, "symbol");
  p.ac = parse_asset_class(get_str(j, "asset_class"));
  p.qty = get_num(j, "qty");
  p.entry = get_num_or(j, "entry", 0);
  p.stop = get_num_or(j, "stop", 0);
  p.mark = get_num_or(j, "mark", p.entry);
  if (p.qty <= 0 || p.mark <= 0) throw ValidationError("position qty/mark must be positive: " + p.symbol);
  return p;
}

AccountState parse_account(const json& j) {
  AccountState a;
  a.equity = get_num(j, "equity");
  a.buying_power = get_num(j, "buying_power");
  if (a.equity <= 0 || a.buying_power < 0) throw ValidationError("equity must be > 0 and buying_power >= 0");
  a.start_day_equity = get_num_or(j, "start_day_equity", a.equity);
  a.start_week_equity = get_num_or(j, "start_week_equity", a.start_day_equity);
  a.start_month_equity = get_num_or(j, "start_month_equity", a.start_week_equity);
  a.high_water_mark = std::max(get_num_or(j, "high_water_mark", a.equity), a.equity);
  a.consecutive_losses = static_cast<int>(get_num_or(j, "consecutive_losses", 0));
  a.trades_today = static_cast<int>(get_num_or(j, "trades_today", 0));
  a.unknown_orders = static_cast<int>(get_num_or(j, "unknown_orders", 0));
  a.last_loss_time = static_cast<int64_t>(get_num_or(j, "last_loss_time", 0));
  a.now = static_cast<int64_t>(get_num(j, "now"));
  a.kill_switch = j.value("kill_switch", false);
  a.critical_failure = j.value("critical_failure", false);
  a.persisted_mode = parse_risk_mode(get_str_or(j, "persisted_mode", "NORMAL"));
  a.operating_mode = get_str_or(j, "operating_mode", "RESEARCH");
  if (a.operating_mode != "RESEARCH" && a.operating_mode != "PAPER" && a.operating_mode != "LIVE")
    throw ValidationError("operating_mode must be RESEARCH, PAPER or LIVE");
  if (j.contains("positions")) for (const auto& p : j["positions"]) a.positions.push_back(parse_position(p));
  if (j.contains("pending_orders")) for (const auto& p : j["pending_orders"]) a.pending_orders.push_back(parse_position(p));
  return a;
}

json ModeEval::to_json() const {
  return json{{"mode", to_string(mode)}, {"drawdown", drawdown}, {"daily_loss", daily_loss},
              {"weekly_loss", weekly_loss}, {"monthly_loss", monthly_loss}, {"reasons", reasons}};
}

ModeEval evaluate_mode(const RiskConfig& cfg, const AccountState& a) {
  ModeEval e;
  auto loss = [&](double start) { return start > 0 ? std::max(0.0, (start - a.equity) / start) : 0.0; };
  e.drawdown = loss(a.high_water_mark);
  e.daily_loss = loss(a.start_day_equity);
  e.weekly_loss = loss(a.start_week_equity);
  e.monthly_loss = loss(a.start_month_equity);
  RiskMode m = RiskMode::Normal;
  auto raise = [&](RiskMode to, const std::string& why) { if (to > m) m = to; e.reasons.push_back(why); };
  if (e.drawdown >= cfg.lock_drawdown) raise(RiskMode::Locked, "drawdown >= lock threshold");
  if (a.critical_failure) raise(RiskMode::Locked, "critical execution failure flagged");
  if (e.daily_loss >= cfg.daily_loss_limit) raise(RiskMode::Locked, "daily loss limit breached");
  if (e.weekly_loss >= cfg.weekly_loss_limit) raise(RiskMode::Locked, "weekly loss limit breached");
  if (e.monthly_loss >= cfg.monthly_loss_limit) raise(RiskMode::Locked, "monthly loss limit breached");
  if (e.drawdown >= cfg.recovery_drawdown) raise(RiskMode::Recovery, "drawdown >= recovery threshold");
  if (e.drawdown >= cfg.caution_drawdown) raise(RiskMode::Caution, "drawdown >= caution threshold");
  if (a.consecutive_losses >= cfg.pause_after_losses) raise(RiskMode::Caution, "consecutive losing trades");
  if (a.persisted_mode > m) raise(a.persisted_mode, "persisted mode " + to_string(a.persisted_mode) + " requires operator unlock");
  e.mode = m;
  return e;
}

Proposal parse_proposal(const json& j) {
  Proposal p;
  p.symbol = get_str(j, "symbol");
  p.strategy = get_str_or(j, "strategy", "");
  p.side = get_str_or(j, "side", "buy");
  p.ac = parse_asset_class(get_str(j, "asset_class"));
  p.entry = get_num(j, "entry");
  p.stop = get_num(j, "stop");
  p.slippage_per_unit = get_num_or(j, "slippage_per_unit", 0);
  p.fee_per_unit = get_num_or(j, "fee_per_unit", 0);
  p.qty_increment = get_num_or(j, "qty_increment", p.ac == AssetClass::Stock ? 1.0 : 1e-8);
  p.min_qty = get_num_or(j, "min_qty", p.qty_increment);
  p.research_only = j.value("research_only", false);
  if (!finite_pos(p.entry) || !finite_pos(p.stop)) throw ValidationError("entry and stop must be positive");
  if (p.qty_increment <= 0 || p.slippage_per_unit < 0 || p.fee_per_unit < 0) throw ValidationError("invalid increment/costs");
  return p;
}

double floor_to_increment(double qty, double inc) {
  if (!(qty > 0) || !(inc > 0)) return 0;
  double n = std::floor(qty / inc + 1e-9);
  double r = std::floor(n * inc * 1e12) / 1e12;      // strip float noise (e.g. 3*0.1)
  while (r > qty) r = std::nextafter(r, 0.0);         // never exceed the requested quantity
  return std::max(0.0, r);
}

json RiskDecision::to_json() const {
  return json{{"decision", approved ? "APPROVED" : "REJECTED"}, {"risk_mode", to_string(mode)},
              {"quantity", quantity}, {"risk_dollars", risk_dollars}, {"notional", notional},
              {"risk_budget", risk_budget}, {"risk_per_unit", risk_per_unit},
              {"reasons", reasons}, {"limits", limits}};
}

static std::string group_of(const RiskConfig& c, const std::string& sym) {
  for (const auto& [g, syms] : c.correlation_groups)
    if (std::find(syms.begin(), syms.end(), sym) != syms.end()) return g;
  return "";
}

RiskDecision check_and_size(const RiskConfig& cfg, const AccountState& a, const Proposal& p) {
  RiskDecision d;
  ModeEval me = evaluate_mode(cfg, a);
  d.mode = me.mode;
  auto reject = [&](const std::string& r) { d.reasons.push_back(r); };

  if (a.operating_mode == "RESEARCH") reject("operating mode RESEARCH: no orders allowed");
  if (a.kill_switch) reject("kill switch engaged");
  if (a.unknown_orders > 0) reject("unreconciled orders in UNKNOWN state");
  if (me.mode == RiskMode::Locked) reject("risk mode LOCKED");
  if (me.mode == RiskMode::Recovery) reject("risk mode RECOVERY: new entries disabled");
  if (p.research_only && (a.operating_mode == "LIVE" || me.mode == RiskMode::Caution))
    reject("research-only strategy not permitted in this mode");
  if (p.side != "buy") reject("only long entries are permitted");
  if (p.stop >= p.entry) reject("stop must be below entry for a long position");
  if (!cfg.allowed_symbols.empty() &&
      std::find(cfg.allowed_symbols.begin(), cfg.allowed_symbols.end(), p.symbol) == cfg.allowed_symbols.end())
    reject("symbol not in allowed watchlist");
  const int open_n = static_cast<int>(a.positions.size() + a.pending_orders.size());
  if (open_n >= cfg.max_open_positions) reject("max simultaneous open positions reached");
  if (a.trades_today >= cfg.max_trades_per_day) reject("max new trades per day reached");
  if (a.last_loss_time > 0 && a.now - a.last_loss_time < cfg.loss_cooldown_sec) reject("post-loss cooldown active");
  for (const auto& x : a.positions)
    if (x.symbol == p.symbol) reject("position already open in symbol (no averaging/pyramiding)");
  for (const auto& x : a.pending_orders)
    if (x.symbol == p.symbol) reject("pending order already exists for symbol");

  // --- sizing ---
  double frac = me.mode == RiskMode::Caution ? cfg.caution_risk_per_trade : cfg.risk_per_trade;
  frac = std::min(frac, cfg.max_trade_risk);
  const std::string grp = group_of(cfg, p.symbol);
  int correlated = 0;
  double stock_exp = 0, crypto_exp = 0;
  auto account = [&](const Position& x, double px) {
    (x.ac == AssetClass::Stock ? stock_exp : crypto_exp) += x.qty * px;
    if (!grp.empty() && group_of(cfg, x.symbol) == grp) ++correlated;
  };
  for (const auto& x : a.positions) account(x, x.mark);
  for (const auto& x : a.pending_orders) account(x, x.entry > 0 ? x.entry : x.mark);
  const double scale = std::pow(cfg.correlated_risk_scale, correlated);
  d.risk_budget = a.equity * frac * scale;
  d.risk_per_unit = (p.entry - p.stop) + p.slippage_per_unit + p.fee_per_unit;
  const double eq = a.equity, tot = stock_exp + crypto_exp;
  const double class_room = (p.ac == AssetClass::Stock ? cfg.max_stock_exposure * eq - stock_exp
                                                       : cfg.max_crypto_exposure * eq - crypto_exp);
  const double total_room = std::min(cfg.max_total_exposure * eq, (1.0 - cfg.min_uncommitted) * eq) - tot;
  const double notional_room = std::min({cfg.max_position_notional * eq, class_room, total_room});
  const double unit_cash = p.entry + p.slippage_per_unit + p.fee_per_unit;
  double q_risk = d.risk_per_unit > 0 ? d.risk_budget / d.risk_per_unit : 0;
  double q_exp = notional_room > 0 ? notional_room / p.entry : 0;
  double q_bp = a.buying_power / unit_cash;
  double q = floor_to_increment(std::min({q_risk, q_exp, q_bp}), p.qty_increment);
  d.limits = json{{"qty_by_risk", q_risk}, {"qty_by_exposure", q_exp}, {"qty_by_buying_power", q_bp},
                  {"correlated_open_positions", correlated}, {"risk_fraction", frac * scale},
                  {"stock_exposure", stock_exp}, {"crypto_exposure", crypto_exp},
                  {"risk_state", me.to_json()}};
  if (q <= 0 || q + 1e-12 < p.min_qty) reject("allowed quantity below minimum tradable quantity");
  d.quantity = q;
  d.risk_dollars = q * d.risk_per_unit;
  d.notional = q * p.entry;
  if (d.risk_dollars > eq * cfg.max_trade_risk + 1e-9) reject("trade risk exceeds max_trade_risk");
  d.approved = d.reasons.empty();  // sizing fields stay populated for diagnostics
  return d;
}

}  // namespace tca
