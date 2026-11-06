---
name: backtest
description: Run the C++ backtester (no look-ahead, costs, chronological train/validation/test, rolling windows, cost sensitivity, bootstrap) on stored data and compare strategies against the acceptance filters. Use for "backtest all strategies".
---

# Backtest

1. Refresh data: `python3 scripts/agent.py fetch-crypto --days 180` (stocks: ingest MCP historicals first).
2. Run `scripts/run-backtest.sh` (or `python3 scripts/agent.py backtest <data file> --include-research`).
3. Read `reports/backtest_<symbol>_<tf>_<date>.summary.json` and the full JSON for per-split metrics.
4. Report trades, net P&L, win rate, profit factor, max drawdown, OOS expectancy, acceptance and failure reasons. Never fabricate or round away losses. A strategy that fails acceptance stays out of LIVE.
