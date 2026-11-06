---
name: trade-report
description: Produce completed trades, realized/unrealized P&L, per-strategy performance, drawdown, risk utilization and execution errors for paper and live journals. Use for "trade report" or "why did the last trades lose".
---

# Trade Report

1. `python3 scripts/agent.py report --days 7` (adjust days).
2. For live P&L use MCP `get_realized_pnl` / `get_pnl_trade_history` and treat them as authoritative.
3. Summarize per strategy, exit reasons, risk mode, and any UNKNOWN orders or gate denials (`state/order_gate_audit.jsonl`).
