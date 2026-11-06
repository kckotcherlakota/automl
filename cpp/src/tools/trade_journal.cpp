// trade_journal: SQLite journal (orders state machine, fills, trades, audit, kv).
#include "tca/journal.hpp"
using namespace tca;
static int run(const json& in, json& out) {
  out = run_journal_action(in);
  out["tool"] = "trade_journal";
  return kOk;
}
int main(int argc, char** argv) { return run_tool(argc, argv, "trade_journal", run); }
