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
### PAPER entry (14:59 ET) — owner asked for a paper trade (live orders blocked by approvals)
- PAPER condor, 1 lot, spot 753.82. Legs (instrument ids):
  sell 755C 662448f9-3052-403b-88d3-f12ca106c239 bid/ask 0.38/0.39 delta 0.28
  buy  756C 75d7f8d3-e6cd-4ff9-9c2b-2630ca1f2da7 bid/ask 0.21/0.22 delta 0.17
  sell 753P ac0508fc-3dce-4412-8385-9fbc51757bfe bid/ask 0.51/0.52 delta -0.37
  buy  752P 867b9f53-a65f-484c-a667-2fe752735171 bid/ask 0.26/0.27 delta -0.22
- mid credit 0.42, natural credit 0.40, collateral ~$58 (RH would hold $100). Approvals now ON
  (setting flipped to true 14:57:37 after the owner reconnected the connector).
- 15:03 test (owner approved): unfillable 4-leg condor (limit credit 0.95) REJECTED with the same
  approval error although get_trade_approval_setting = false (updated 15:02:07). Nothing placed.
  Conclusion: the setting the agent reads is not what blocks agent multi-leg orders. Needs
  Robinhood support; until fixed the bot cannot trade condors live.
### PAPER close (15:36 ET)
- No live/manual position open (owner did not place the condor). SPY 754.57.
- 755C 0.43/0.44, 756C 0.17/0.18, 753P 0.15/0.16, 752P 0.06/0.07 -> natural debit 0.37, mid 0.35.
- PAPER P&L: +$4.84 (mid credit, natural close), +$2.84 (natural/natural), +$6.84 (mid/mid). WIN.
- Learnings: real SPY 0DTE legs were 1c wide all afternoon (bid/ask), consistent with the model's
  half-spread; with 37 min held (late entry 14:59) the condor still captured ~$5-7 of decay.
### Close (15:38 ET)
- No open position (no live condor; owner did not place the manual one). Nothing to close.
- Account value $274.75. LIVE totals since 2026-11-09: 0 trades, $0. No kill switch.
### Handoff for tomorrow
- Tue 2026-11-10 = October CPI -> EVENT DAY: SKIP (morning prep should write it and email).
- Next tradable day Wed 2026-11-11 (Veterans Day; stocks/options open).
- BLOCKER: Robinhood rejects agent multi-leg orders (6 rejections today, approvals off). Expect
  the Wed entry to be rejected; skip, email, never leg in. Owner decision pending: stay LIVE,
  switch to PAPER, or move to a broker that accepts agent condors (Public suggested).
- Other agent-initiated single-call trades hit this account today (IWM/SPY, -$9.54). If an
  unknown position is open at entry, skip.
- New: PAPER single-leg tracker routine at 16:07 ET (PLAYBOOK §9).
### PAPER single-leg (after close)
- OR 09:30-09:45: high 746.785, low 741.09. Up-break armed 09:45-09:50 bar (close 747.065, vol 506k
  > vol20 497k); retest + confirm on 09:50-09:55 bar (low 746.805 <= OR high x1.0005, close 747.635,
  above VWAP, EMA9>EMA21) -> LONG signal 09:55 ET. No second signal before 11:30.
- Bought SPY 2026-11-09 749C (first OTM strike) at 1.52+0.01 = 1.53 (09:55 bar close, mark bars).
  Target 2.14 / stop 1.07. Target hit in the 10:40-10:45 bar (high 2.24, open 1.79) -> exit 2.14.
- P&L +$61.12 (target). Caveat: option bars are marks; real fills may differ.
- single-leg paper total since 2026-11-09: 1 trades, 1 wins, $61.12

## 2026-11-10 (Tue)
### Morning prep (~09:00 ET)
- Account MCP ••••8852: option_level_3, limited margin, BP $274.75, no positions, approvals OFF.
- Last daily report 2026-11-09 present, no KILL SWITCH. Program now V3 micro-$200 (paper only).
- CPI verified from BLS (primary) 0.4 / core 0.3 vs consensus 0.3 / 0.2 (CNBC as-of 11-09 16:03) ->
  HOT. EVENT DAY. V3: first entry 10:00 ET, PAPER_ONLY_ON_MACRO_DAY.
- Premarket 09:12: SPY 748.50 (-0.6%), QQQ 672.26 (-1.0%). Not HIGH RISK.
- LIVE=REQUESTED_NOT_READY (docs/LIVE_READINESS_2026-11-10.md: gates 1,3,4,5 fail, 2 partial).
- PLAN: EVENT DAY: SKIP CPI (live). Paper V3-A replay after close.
