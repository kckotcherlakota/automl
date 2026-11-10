# Robinhood MCP capabilities — observed in this session (as of 2026-11-10)

Connector: claude.ai "Robbinhood" connector (tools `mcp__Robbinhood__*`). Account tradable by this
agent: "MCP" ••••8852 (limited margin, option_level_3). All other accounts read-only/unavailable.

| Capability | Observed |
|---|---|
| Accounts, portfolio/BP, positions, orders, quotes, chains, instruments | Work |
| Option historicals (5-min, mark OHLC, no volume, no bid/ask) | Work — not NBBO |
| Equity historicals (5-min/1-min OHLCV) | Work |
| get_trade_approval_setting | Works; reads OFF since 2026-11-09 20:02Z, but did not match enforcement |
| review_option_order (4-leg) | Works, no order_checks |
| place_option_order multi-leg | **Rejected 6×** ("requires the customer to approve agent-placed trades, and a multi-leg strategy cannot be sent for approval yet") |
| place_option_order single-leg buy-to-open / sell-to-close | **Not yet tested by this bot.** Another agent's single-leg orders filled on this account 2026-11-09 |
| Contract force-close time | `sellout_datetime` 20:45Z (15:45 ET) on SPY 0DTE |
| Fees (review) | $0.02 OCC + $0.02 ORF per contract-leg |
Robinhood help: agents place "long equities, options, and crypto orders"; "Certain trades may still
require your approval even when trade approvals are turned off".
