#include "tca/execution.hpp"

#include <cmath>

namespace tca {

ExecConfig parse_exec_config(const json& j) {
  ExecConfig c;
  if (!j.is_object()) return c;
  c.max_spread_bps = get_num_or(j, "max_spread_bps", c.max_spread_bps);
  c.max_quote_age_sec = get_num_or(j, "max_quote_age_sec", c.max_quote_age_sec);
  c.max_move_bps = get_num_or(j, "max_move_bps", c.max_move_bps);
  c.aggressiveness = get_num_or(j, "aggressiveness", c.aggressiveness);
  c.slippage_bps = get_num_or(j, "slippage_bps", c.slippage_bps);
  c.min_imbalance = get_num_or(j, "min_imbalance", c.min_imbalance);
  c.max_order_age_sec = static_cast<int>(get_num_or(j, "max_order_age_sec", c.max_order_age_sec));
  c.max_retries = static_cast<int>(get_num_or(j, "max_retries", c.max_retries));
  c.min_retry_delay_sec = static_cast<int>(get_num_or(j, "min_retry_delay_sec", c.min_retry_delay_sec));
  if (c.aggressiveness < 0 || c.aggressiveness > 1) throw ValidationError("aggressiveness must be in [0,1]");
  if (c.max_spread_bps <= 0 || c.max_quote_age_sec <= 0) throw ValidationError("invalid execution limits");
  return c;
}

Quote parse_quote(const json& j) {
  Quote q;
  q.bid = get_num(j, "bid");
  q.ask = get_num(j, "ask");
  q.bid_size = get_num_or(j, "bid_size", 0);
  q.ask_size = get_num_or(j, "ask_size", 0);
  q.ts = static_cast<int64_t>(get_num(j, "ts"));
  if (!finite_pos(q.bid) || !finite_pos(q.ask)) throw ValidationError("bid/ask must be positive");
  if (q.ask < q.bid) throw ValidationError("crossed quote: ask < bid");
  if (q.bid_size < 0 || q.ask_size < 0) throw ValidationError("negative quote size");
  return q;
}

double round_to_tick(double px, double tick, bool down) {
  if (!(tick > 0)) throw ValidationError("tick must be positive");
  double n = px / tick;
  n = down ? std::floor(n + 1e-9) : std::ceil(n - 1e-9);
  return std::round(n * tick * 1e10) / 1e10;
}

json ExecPlan::to_json(const ExecConfig& c) const {
  return json{{"decision", ok ? "OK" : "REJECTED"}, {"limit_price", limit_price}, {"mid", mid},
              {"spread_bps", spread_bps}, {"move_from_signal_bps", move_bps}, {"book_imbalance", imbalance},
              {"est_fill_slippage", est_fill_slippage}, {"reasons", reasons},
              {"order_constraints", {{"order_type", "limit"}, {"max_order_age_sec", c.max_order_age_sec},
                                     {"max_retries", c.max_retries}, {"min_retry_delay_sec", c.min_retry_delay_sec},
                                     {"on_expiry", "cancel; reconcile status before any retry"}}}};
}

ExecPlan plan_entry(const ExecConfig& c, const Quote& q, double signal_price, double tick, int64_t now,
                    int retries_used) {
  ExecPlan p;
  if (!finite_pos(signal_price)) throw ValidationError("signal_price must be positive");
  p.mid = (q.bid + q.ask) / 2.0;
  p.spread_bps = (q.ask - q.bid) / p.mid * 1e4;
  p.move_bps = (p.mid - signal_price) / signal_price * 1e4;
  const double sz = q.bid_size + q.ask_size;
  p.imbalance = sz > 0 ? (q.bid_size - q.ask_size) / sz : 0;
  if (now - q.ts > c.max_quote_age_sec) p.reasons.push_back("quote is stale");
  if (q.ts > now + 2) p.reasons.push_back("quote timestamp in the future");
  if (p.spread_bps > c.max_spread_bps) p.reasons.push_back("spread too wide");
  if (p.move_bps > c.max_move_bps) p.reasons.push_back("price ran away from signal (do not chase)");
  if (sz > 0 && p.imbalance < c.min_imbalance) p.reasons.push_back("order book ask-heavy (microstructure filter)");
  if (retries_used > c.max_retries) p.reasons.push_back("max order retries exhausted");
  double lim = p.mid + c.aggressiveness * (q.ask - q.bid) / 2.0;
  lim = std::min(lim, q.ask);
  p.limit_price = round_to_tick(lim, tick, true);
  if (p.limit_price < q.bid) p.limit_price = round_to_tick(q.bid, tick, true);
  p.est_fill_slippage = (p.limit_price - p.mid) + p.mid * c.slippage_bps / 1e4;
  p.ok = p.reasons.empty();
  return p;
}

}  // namespace tca
