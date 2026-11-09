# Daily notes (current week)

Written by the bot during each trading day. Cleared every Friday by the weekly review, after
its contents are summarized into `long_term_notes.md`. Newest day at the bottom.

Format per day:

```
## YYYY-MM-DD (Mon)
### Morning prep (~09:00 ET)
- account/BP, open positions, market open, event day, market context (futures/VIX/headlines),
  approvals, kill switch; sources used (and any that failed)
- PLAN: TRADE | EVENT DAY: SKIP <event> | SKIP: <reason>
### Entry (14:30 ET)
- spot, strikes, legs bid/ask/mid/delta, mid & natural credit, filled credit, order id, collateral
### Close (15:38 ET)
- closing debit (filled & natural), P&L, win/loss, slippage per leg; running total / equity
### Learnings
- real vs model; data or tool problems; ideas (untested = say so)
### Handoff for tomorrow
- next trading day + event day?; open issues; sources that failed; what morning prep must check
```

---


## 2026-11-09 (Mon)
### Morning prep (~09:00 ET)
- Options Level 3: yes. Market open, normal day; SPY 0DTE expiration 2026-11-09 listed.
- Account value / buying power $284.29 (up from $204.29: +$80 deposit). Bot budget stays capped at
  $200 (owner rule) -> 1 contract, collateral <= $200.
- Open option positions: none. No earlier "0DTE daily report" (first live day) -> no kill switch.
- Event filter (web search of bls.gov / federalreserve.gov calendars): no CPI, FOMC or jobs report
  today -> normal day. Tomorrow Tue 2026-11-10 = October CPI 08:30 ET -> EVENT DAY, skip.
- Trade approvals: OFF -> orders execute without owner tap. ATTENTION email sent.
- Plan 14:30: trade 1 SPY condor (0.30 delta, $1 wings) if mid credit >= $0.15; close 15:38-15:43.
