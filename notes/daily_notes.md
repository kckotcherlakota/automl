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
- Market context (Finviz, 09:22 ET): SPY 741.67 (+0.87% pre-open) after last week's -1.4% selloff;
  Dow futures +600 on lower oil; Nvidia falling on a CEO probe. RSI 39, ATR(14) $10.30. Not HIGH RISK.
- PLAN: TRADE (1 SPY condor, 0.30 delta, $1 wings, if mid credit >= $0.15; close 15:38-15:43).
### Entry (14:30 ET) — NOT PLACED
- 14:35 spot 752.38. Condor: -753C/+754C (deltas 0.35/0.17), -751P/+750P (-0.23/-0.13).
  Mid credit 0.34 -> 0.33 at 14:36, natural 0.32. Review OK (no alerts, fees $0.16, RH collateral $100).
- place_option_order REJECTED: "account requires the customer to approve agent-placed trades; a
  multi-leg strategy cannot be sent for approval yet". get_trade_approval_setting still says OFF.
  Did not leg in (risk of an unhedged short). Skip email sent.
- Other agent-initiated orders on MCP today, NOT placed by this bot: IWM 280/281/282C round trips
  10:04-11:54 ET and SPY 754C (buy 0.46 13:50, sell 0.37 13:53). All flat, net about -$9.54
  (BP $284.29 -> $274.75). Owner informed.
- 14:41 owner turned approvals off (setting updated 14:40:59 ET). Retried: spot 754.94, condor
  -756C/+757C (0.34/0.24 delta), -753P/+752P (-0.26/-0.16); mid 0.41, natural 0.38, limit 0.40.
  Review OK, but place rejected TWICE with the same approval error (setting reads OFF). No order exists.
  Owner asked to retry at 14:44, 14:52 and 14:56 (754/755C + 752/751P etc., mid 0.39-0.46):
  all rejected with the same approval error. 5 rejections total; no order exists. No condor today.
### Learnings
- Robinhood can require approval for agent orders even when the setting reads OFF; multi-leg
  orders cannot be approved -> the condor cannot be placed until the owner removes that.
- Another agent trades this same account. Positions/BP checks must assume foreign activity.
### Handoff for tomorrow
- Tue 2026-11-10 = CPI -> EVENT DAY: SKIP. Next tradable day Wed 2026-11-11 (Veterans Day, market open).
- Open issue: placement still blocked after the owner turned approvals off at 14:40:59 ET (maybe
  the connector needs a fresh connection). Wed: if place is rejected again the same way, skip,
  email the owner to reconnect the Robinhood connector in claude.ai settings. Never leg in.
- Open issue: foreign agent trades on MCP. If an unknown position is open at entry, skip.
