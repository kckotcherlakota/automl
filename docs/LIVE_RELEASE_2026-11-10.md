# Operator live release — 2026-11-10 (single session)

Operator (account owner) instruction, received ~09:15 ET 2026-11-10, verbatim:
> "No run live today yes you are approved only for level 2 just stick to the strategy I gave you"

This overrides V3 §15 gates 1 (backtest/paper evidence) and 5 (CPI-day evidence), which the operator
was shown in docs/LIVE_READINESS_2026-11-10.md and the 09:12 email. Scope of this release:

| Item | Value |
|---|---|
| Date | 2026-11-10 only (expires at 11:45 ET; next session needs a new release) |
| Account | Robinhood "MCP" ••••8852 only |
| Instruments | SPY/QQQ 0DTE, BUY-TO-OPEN call or put, SELL-TO-CLOSE owned contracts. Level 2 only: no spreads, no shorts |
| Strategy | V3 Strategy A (ORB + VWAP trend continuation), rules per docs/MICRO_200_V3_RULEBOOK.md §3 |
| Entry window | 10:00–11:30 ET (CPI day); deadline liquidation from 11:40; broker-confirmed flat by 11:45 |
| Contract | |delta| 0.20–0.30 (broker greeks, timestamped), prefer 0.25; spread ≤ $0.03 and ≤ 15% of mid; 1 contract |
| Budget | $40 envelope: cumulative filled buy debits + pending + $5 contingency ≤ $40 → $35 usable; wins never replenish |
| Exits | take profit net ≥ max($5, 25% debit); arm trail at max($3, 15%), exit on giveback ≥ max($1.50, 30% peak); stop bid ≤ 0.75×entry or net loss ≥ $8; structure invalidation; 12 completed 1-min bars; 11:40 deadline |
| Max entries | 2 filled per day, 1 open at a time |
| No trade if | no signal, unaffordable, bad spread/quote, broker rejects, any unknown order state |
| Monitoring | operated interactively from this Claude session by polling Robinhood quotes (seconds-to-a-minute granularity, NOT a real-time engine); a 11:41 ET safety reminder forces a flatness check |
