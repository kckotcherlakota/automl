# Daily notes (current week)

Written by the bot during each trading day. Cleared every Friday by the weekly review, after
its contents are summarized into `long_term_notes.md`. Newest day at the bottom.

Format per day:

```
## YYYY-MM-DD (Mon)
### Morning prep (~09:00 ET)
- buying power, open positions, holiday/half-day, event day (FOMC/CPI/NFP), kill-switch status
- plan: which symbols to price at 14:30, anything to watch
### Bull put entry (09:45 ET)
- spot, strikes, legs bid/ask, mid & natural credit, collateral, VIX, both flags
### Entry (14:30 ET)
- per symbol: spot, strikes, mid/natural credit, collateral, SKIP reason
### Snapshot 15:35 ET / ### Snapshot 15:40 ET
- per symbol: spot, legs bid/ask, natural & mid closing debit, P&L (mid & natural credit)
### Bull put close (15:40 ET)
- closing debit, P&L (mid & natural credit), flags
### Close (15:50 ET)
- per symbol: closing debit, P&L (mid & natural), win/loss; running total / equity
### Learnings
- what went differently from the model / plan; data or tool problems; ideas (untested = say so)
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
