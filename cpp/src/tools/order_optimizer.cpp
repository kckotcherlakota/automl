// order_optimizer: spread / staleness / chase checks and a limit price.
// exit 0 = OK, 3 = REJECTED.
#include "tca/execution.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  ExecConfig cfg = parse_exec_config(in.value("config", json::object()));
  Quote q = parse_quote(in.at("quote"));
  const double tick = get_num(in, "tick_size");
  ExecPlan p = plan_entry(cfg, q, get_num(in, "signal_price"), tick, static_cast<int64_t>(get_num(in, "now")),
                          static_cast<int>(get_num_or(in, "retries_used", 0)));
  out = p.to_json(cfg);
  out["tool"] = "order_optimizer";
  return p.ok ? kOk : kRejected;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "order_optimizer", run); }
