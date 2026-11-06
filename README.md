# Claude Code Trading Agent

Claude Code is the agent; C++20 tools do the deterministic work; Robinhood MCP is the broker connection.
**Default mode is RESEARCH. Nothing here places real orders.** See `CLAUDE.md` for the rules and
`docs/MASTER_PROMPT.md` for the full brief.

## Quick start
```bash
scripts/bootstrap.sh            # deps + build + all tests
python3 scripts/agent.py fetch-crypto --days 180
scripts/run-backtest.sh         # writes reports/backtest_*.summary.json
python3 scripts/agent.py scan   # live signal scan (no orders)
scripts/run-paper.sh            # one PAPER cycle (simulated fills)
python3 scripts/agent.py readiness
```
In Claude Code: "scan the market", "run one paper trading cycle", "show my current risk",
"backtest all strategies", "stop new trading immediately".

## Components
| Path | Purpose |
|---|---|
| `cpp/` | indicators, strategies A–G, regime, risk/drawdown, execution pricing, backtester, SQLite journal, tests, benchmark |
| `scripts/agent.py` | orchestration CLI (data, paper account, mode, kill switch, authorizations) |
| `.claude/hooks/pre_order_gate.py` | fail-closed PreToolUse gate on every Robinhood order tool |
| `.claude/skills/` | `/market-scan`, `/trade-cycle`, `/risk-check`, … (plus the claude-trading-skills library) |
| `config/` | risk limits, strategy enablement, watchlist, costs/execution limits |

## Current verdict (2026-11-06)
- 0.95% per-side Robinhood crypto fee (measured) makes 15m strategies uneconomic; the cost filter rejects them.
- 180-day BTC/ETH backtests: no strategy passes acceptance (PF < 1 even with zero fees).
- LIVE stays disabled. Paper trading continues to collect evidence.
