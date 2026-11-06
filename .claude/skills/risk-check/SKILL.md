---
name: risk-check
description: Report equity, buying power, exposure, daily/weekly P&L, drawdown, risk mode (NORMAL/CAUTION/RECOVERY/LOCKED), pending orders and whether new entries are allowed. Use for "show my buying power and current risk".
---

# Risk Check

1. Paper account: `python3 scripts/agent.py risk-check`.
2. Live account: fetch MCP `get_portfolio`, `get_crypto_positions`, `get_equity_positions` and open orders for account 537538852 (MCP account only). Write a snapshot JSON with fields `equity, buying_power, start_day_equity, start_week_equity, start_month_equity, high_water_mark, consecutive_losses, trades_today, positions[]` to `state/live_snapshot.json`, then run `python3 scripts/agent.py risk-check --live-snapshot state/live_snapshot.json`.
3. Report the risk mode, the reasons, and whether new entries are allowed. Broker data is authoritative over local state.
