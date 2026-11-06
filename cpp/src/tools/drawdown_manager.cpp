// drawdown_manager: NORMAL/CAUTION/RECOVERY/LOCKED state machine.
// The persisted mode never relaxes with time; only an explicit operator unlock
// can lower it, and never below what current account metrics require.
#include "tca/risk.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  RiskConfig cfg = parse_risk_config(in.value("config", json::object()));
  AccountState a = parse_account(in.at("account"));
  RiskMode persisted = a.persisted_mode;
  AccountState fresh = a; fresh.persisted_mode = RiskMode::Normal;
  ModeEval metrics_only = evaluate_mode(cfg, fresh);
  ModeEval combined = evaluate_mode(cfg, a);
  RiskMode next = combined.mode;
  json unlock = in.value("operator_unlock", json::object());
  bool unlocked = false;
  if (unlock.value("authorized", false)) {
    if (get_str_or(unlock, "by", "").empty() || get_str_or(unlock, "reason", "").empty())
      throw ValidationError("operator_unlock requires 'by' and 'reason'");
    next = metrics_only.mode;
    unlocked = next < persisted;
  }
  out = {{"tool", "drawdown_manager"}, {"previous_persisted_mode", to_string(persisted)},
         {"metrics_mode", metrics_only.to_json()}, {"new_persisted_mode", to_string(next)},
         {"operator_unlock_applied", unlocked},
         {"new_entries_allowed", next == RiskMode::Normal || next == RiskMode::Caution},
         {"risk_per_trade", next == RiskMode::Caution ? cfg.caution_risk_per_trade
                            : (next == RiskMode::Normal ? cfg.risk_per_trade : 0.0)}};
  return kOk;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "drawdown_manager", run); }
