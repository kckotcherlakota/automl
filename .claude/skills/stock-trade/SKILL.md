---
name: stock-trade
description: Evaluate a stock/ETF setup end-to-end (signal, execution quality, risk sizing) and, only in an authorized LIVE session, prepare a gated Robinhood equity order. Use for "evaluate SPY using opening range breakout" or stock entries.
---

# Stock Trade

1. Check mode: `cat state/mode.json` (missing = RESEARCH). In RESEARCH/PAPER stop after step 5 and report.
2. Fetch fresh 5m bars via MCP `get_equity_historicals` and ingest (see `/market-scan`).
3. Run `build/signal_engine` (via `scripts/agent.py scan`) on the symbol. No qualified signal => stop and explain the failed checks.
4. Get a live quote with MCP `get_equity_quotes`; run `build/order_optimizer` with `config/trading.json` stock execution limits. Rejected => stop.
5. Build the account snapshot from MCP `get_portfolio`, `get_equity_positions`, `get_equity_orders` and run `build/risk_engine`. Report quantity, risk dollars, invalidation level and management plan.
6. LIVE only: write the request JSON and run `python3 scripts/agent.py authorize-order <request.json>`. Then call `review_equity_order`, re-check the quote, and call `place_equity_order` with exactly the returned quantity, limit price and `ref_id`. The PreToolUse hook denies anything else.
7. Whole shares only for limit orders (Robinhood fractional shares require market orders, which this agent does not use for entries).
8. After submission: never treat a timeout as failure; reconcile with `get_equity_orders` before any retry; journal transitions with `build/trade_journal`.
