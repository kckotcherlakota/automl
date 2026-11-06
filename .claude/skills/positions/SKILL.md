---
name: positions
description: Retrieve positions and orders via Robinhood MCP, reconcile with the local journal, and flag anything needing attention (UNKNOWN orders, unprotected positions, mismatches). Use for "show current orders and positions".
---

# Positions

1. `python3 scripts/agent.py positions` for paper + journal view.
2. MCP: `get_crypto_positions`, `get_crypto_orders`, `get_equity_positions`, `get_equity_orders` for the MCP account only.
3. Reconcile: every live position should have a broker-accepted protective stop; any journal order in UNKNOWN must be matched to broker order history and transitioned (`build/trade_journal` action `transition`). Until reconciled, no new entries.
4. Report mismatches explicitly.
