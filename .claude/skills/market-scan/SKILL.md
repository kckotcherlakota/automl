---
name: market-scan
description: Scan the stock and crypto watchlists with the C++ signal engine and report regime, qualified setups, rejected setups and whether any trade qualifies. Use for "scan the market", "find qualified opportunities", "analyze BTC and ETH".
---

# Market Scan

1. Read `CLAUDE.md` operating rules. Do not place orders from this skill.
2. Stocks (optional, regular hours only): call Robinhood MCP `get_equity_historicals` with `interval: "5minute"`, `bounds: "regular"` for the watchlist in `config/watchlist.json` (start at least 3 sessions back). Save the raw response to `data/stocks/raw_<date>.json`, then run `python3 scripts/agent.py ingest-mcp-bars data/stocks/raw_<date>.json`.
3. Run `python3 scripts/agent.py scan` (crypto bars come from the public Coinbase API; stock bars from step 2).
4. Report per symbol: regime, data age, qualified strategies (entry/stop/target/rr_net) and the failed checks for each rejected strategy. Say plainly when nothing qualifies.
5. A qualified signal is not an order. Route to `/stock-trade` or `/crypto-trade` only if the operator asks.
