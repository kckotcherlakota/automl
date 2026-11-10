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
| Strategy | V3 Strategies A (ORB+VWAP, §3), B (SMT sweep reversal at frozen ORH/ORL, §4) and C (trend-filtered SMT pullback, prior-20-bar levels, §5). Operator added SMT at ~09:27 ET ("Use SMT techniques"). First valid signal from any takes the single position slot. Code: research/live/strat_a.py, research/live/strat_smt.py |
| Entry window | 10:00–11:30 ET (CPI day); deadline liquidation from 11:40; broker-confirmed flat by 11:45 |
| Contract | |delta| 0.20–0.30 (broker greeks, timestamped), prefer 0.25; spread ≤ $0.03 and ≤ 15% of mid; 1 contract |
| Budget | $40 envelope: cumulative filled buy debits + pending + $5 contingency ≤ $40 → $35 usable; wins never replenish |
| Exits | take profit net ≥ max($5, 25% debit); arm trail at max($3, 15%), exit on giveback ≥ max($1.50, 30% peak); stop bid ≤ 0.75×entry or net loss ≥ $8; structure invalidation; 12 completed 1-min bars; 11:40 deadline |
| Max entries | 2 filled per day, 1 open at a time |
| No trade if | no signal, unaffordable, bad spread/quote, broker rejects, any unknown order state |
| Monitoring | operated interactively from this Claude session by polling Robinhood quotes (seconds-to-a-minute granularity, NOT a real-time engine); a 11:41 ET safety reminder forces a flatness check |

## Scope addendum ~09:35 ET (operator)
Operator, verbatim: "For company stocks, plan for some option and try selling it on the same day. Like
whatever option that you feel is worth it, take that option, and if you get profits, you can sell it.
So plan accordingly when you do company stocks. When you do ETFs, do zero DTE."

Implemented deterministically (no discretionary picks — V3 forbids LLM-invented signals):
- ETFs SPY/QQQ: 0DTE, Strategies A/B/C (unchanged).
- Company stocks: watchlist AAPL, NVDA, TSLA, AMD, META, AMZN, MSFT, PLTR. Signal = Strategy A
  (ORB+VWAP+EMA, same thresholds) on the stock itself; SMT not used for single names (needs a pair).
  Contract = nearest listed expiration (Fri 2026-11-13), BUY-TO-OPEN call/put, |delta| 0.20–0.30,
  V3 spread/quote filters, 1 contract.
- Same shared $40 envelope ($35 usable), 1 open position at a time, ≤2 filled entries/day in total.
- Exits identical to V3 §7.2, and **always closed the same session** (deadline 11:40, flat by 11:45),
  even at a loss: V3 forbids overnight exposure.

## Delta addendum ~10:05 ET (operator)
Operator, verbatim: "Try 40 delta or affordable delta for spy qqq"
- SPY/QQQ contract selection (overrides V3 §6 band for today): choose the HIGHEST |delta| ≤ 0.40 whose
  ask fits the remaining budget (100 × ask + $0.04 fees ≤ $35 − used), floor |delta| ≥ 0.10 (bot
  safety floor, operator may change). Spread ≤ $0.03 and ≤ 15% of mid still required.
- Observed at 09:58: 0.29Δ 753C $0.92, 0.22Δ 754C $0.65, −0.24Δ 746P $0.83, −0.20Δ 745P $0.66 — all over
  $35; a 0.40Δ contract costs ~$1.2+, so selection will normally land in ~0.10–0.20Δ this morning.
- Company stocks unchanged (0.20–0.30Δ nearest expiry).

## Capital-protection addendum ~10:09 ET (operator)
Operator, verbatim: "Keep in mind 200$ is all you have and capital must be secured"
Tightened (allowed by V3 §7.1 "operator may request a smaller limit"):
- Max **1 filled entry per day** (was 2). After any fill, no new entries that session.
- **Program stop-loss:** cumulative live bot P&L ≤ −$40 (20% of the $200 bot capital) → live disabled
  until the operator re-approves in writing. Tracked in notes/long_term_notes.md.
- Unchanged: ≤ $35 premium/day + $5 fee contingency (worst day ≈ −$40), $8 / −25% loss cut, no
  averaging down, no size increase after losses, profits never raise limits, flat by 11:45.

## AMZN budget addendum ~10:12 ET (operator)
Operator, verbatim: "80$ for mamazon" (answer to: 1=$80 per stock trade, 2=$120, 3=keep $35).
- AMZN option trades: max premium **$80** (100 × ask + fees ≤ $80), 1 contract, 0.20–0.30 delta nearest
  expiry, spread ≤ $0.03 and ≤ 15% of mid (unchanged), same exits, same-day close.
- Program stop-loss raised to cumulative −$80 (as proposed with option 1). Still max 1 filled entry/day.
- Other stocks stay at $35; SPY/QQQ stay at $35 with affordable delta ≤0.40 (floor 0.10).
- The 10:09 AMZN signal is NOT taken: spread $0.05 > $0.03 and the signal is stale (no chasing).

## Stock budget addendum ~10:13 ET (operator)
Operator, verbatim: "For stocks it's 80$"
- ALL watchlist stocks (AAPL, NVDA, TSLA, AMD, META, AMZN, MSFT, PLTR): max premium $80 per trade
  (100 × ask + fees ≤ $80). Other stock rules unchanged (0.20–0.30 delta nearest expiry, spread ≤ $0.03
  and ≤ 15% mid, same exits, same-day close). SPY/QQQ stay $35. 1 entry/day; program stop −$80.
