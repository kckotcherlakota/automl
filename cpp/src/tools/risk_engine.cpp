// risk_engine: deterministic pre-trade validation and position sizing.
// exit 0 = APPROVED, 3 = REJECTED.
#include "tca/risk.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  RiskConfig cfg = parse_risk_config(in.value("config", json::object()));
  AccountState a = parse_account(in.at("account"));
  if (get_str_or(in, "action", "check") == "mode") {
    out = evaluate_mode(cfg, a).to_json();
    out["tool"] = "risk_engine";
    return kOk;
  }
  Proposal p = parse_proposal(in.at("proposal"));
  RiskDecision d = check_and_size(cfg, a, p);
  out = d.to_json();
  out["tool"] = "risk_engine";
  out["symbol"] = p.symbol;
  out["operating_mode"] = a.operating_mode;
  return d.approved ? kOk : kRejected;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "risk_engine", run); }
