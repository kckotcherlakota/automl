---
name: crypto-trade
description: Evaluate BTC/ETH setups (strategies E/F) with live fee-aware costs, execution checks and risk sizing; in an authorized LIVE session prepare a gated Robinhood crypto order. Use for crypto momentum/pullback trade requests.
---

# Crypto Trade

1. Check `state/mode.json` and `state/kill_switch.json`.
2. Run `python3 scripts/agent.py scan` and read the BTC-USD / ETH-USD sections.
3. Fees: this account pays ~0.95% per side (Exchange Routing, measured with `preview_crypto_order`). The signal engine rejects any setup whose round-trip cost exceeds 25% of the stop distance. Expect most 15m setups to be rejected; say so rather than loosening parameters.
4. If a signal qualifies: quote via MCP `get_crypto_quotes` (with `rhs_account_number`), run `order_optimizer`, then `risk_engine` with the live snapshot (`get_portfolio`, `get_crypto_positions`, `get_crypto_orders`).
5. LIVE only: `python3 scripts/agent.py authorize-order <request.json>`, then `preview_crypto_order`, then `place_crypto_order` with the authorized quantity/limit/ref_id. After the fill, place a broker-side `stop_loss` sell and confirm it was accepted before calling the position protected.
6. In PAPER mode use `/trade-cycle` instead.
