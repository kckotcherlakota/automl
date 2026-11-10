# Long-term notes

Durable lessons and running statistics. Updated once a week (Friday weekly review) from
`daily_notes.md` before that file is cleared. Read at the start of every trading day.

## 2026-11-10 PROGRAM CHANGE: Level 2 single-leg only (docs/SPEC_single_leg_0DTE_v1.md). Condor
## entry trig_01U4piD2uwooiZf8YGWNP81z and close trig_01PQpQw3NJam4eySAJ8ZS2f7 DISABLED. Live disabled.

## Setup (routine IDs; all run in session session_012ZPnNbEARMTpMMQ1Weeq33, New York time)
- LIVE from 2026-11-09, SPY condor only (QQQ off), close by 15:44 (Robinhood auto-closes at-risk
  expiring options from 15:45). Owner chose: 15:40-style close, SPY only, bull put dropped.
- trig_01R4cdsFSJEjPmRR2157m314  0DTE morning prep (LIVE, SPY)        09:07 Mon-Fri
- trig_01U4piD2uwooiZf8YGWNP81z  0DTE condor entry (LIVE, SPY)        14:30 Mon-Fri
- trig_01PQpQw3NJam4eySAJ8ZS2f7  0DTE condor close (LIVE, SPY, 15:38) 15:38 Mon-Fri (retries to 15:43)
- trig_0161yiuFh5aixJVJpB5BcwFo  0DTE weekly review                   16:17 Fri
- DISABLED: trig_01S6N4ou9mVZ43FRWDpuuSYG (15:35 snapshot), trig_01DGWykto5NnYstHFDaBowch (15:40
  snapshot + bull put close), trig_01PKAjbj1ZarjNSZDcfas9VE (bull put entry),
  trig_01Y8kAHGdihXxhLSj55KPLCX (daily /clear: does not work from a routine).
- BUDGET: owner's hard cap $200 for this bot, ever. Always 1 contract, collateral <= min(BP, $200),
  no scaling with account size, no extra deposits for this bot.
- Trade approvals were OFF on 2026-11-08: orders execute without a tap in the app.
- Prompts: routine_prompts.md.

## Event days (skip in LIVE: CPI, FOMC decision, jobs report) — keep this list current
- Sources: finviz.com/calendar/economic (best: whole week), bls.gov/schedule, federalreserve.gov.
  Checked 2026-11-09. FMP connector has no economic calendar. Info-only (no skip): PPI, Fed
  speakers, jobless claims.
- 2026-11-10 Tue: October CPI 08:30 ET -> SKIP
- 2026-12-09 Wed: FOMC decision 14:00 ET -> SKIP
- 2026-12-10 Thu: November CPI 08:30 ET -> SKIP
- 2026-11-13 Fri: October PPI 08:30 ET (info only, trade normally)
- October jobs report already released (~2026-11-06). Next (November) jobs report: likely
  early December (~2026-12-04), UNCONFIRMED -> morning prep must confirm.
- 2026-11-11 Wed Veterans Day: stocks/options OPEN (bond market closed) -> trade normally.

## Running totals (LIVE from 2026-11-09, start equity $204.29)
| week ending | trades | wins | win % | week P&L (mid) | week P&L (natural) | cumulative P&L (natural) | peak | drawdown |
|---|---|---|---|---|---|---|---|---|

## Real vs model credit
- Model: typical credit $0.35–0.45 per condor, collateral ~$61. (Go-live rule: real credits
  within ~15% of model and total paper P&L not clearly negative.)

## Lessons (keep short, newest first)
- 2026-11-10: Strategy search (model prices, close 15:40, +1c/leg, 994 days). CONDORS: best is SPY
  0.30d / $2 wings / enter 11:00-12:00 / close 15:40: +$13-14/trade after costs, all 4 years +,
  OOS +$12-17, 69-72% winning weeks; but RH holds $200 collateral (= the whole cap). 14:30 $1-wing
  condor is -$2/trade after costs. SINGLE-LEG long calls/puts (3,456 configs: ORB, trend, fade,
  open-direction): only 46 positive after costs (~chance level); survivors win 9-20%, top 10 trades
  = 240-290% of all profit, losing streaks 18-56, a $200 account dies within days. Not viable.
- 2026-11-09: Agent multi-leg orders rejected ("requires the customer to approve agent-placed
  trades") 6 times, including after the owner turned approvals off (setting read false) and after
  reconnecting. The readable setting does not reflect the block. Single-leg agent orders from
  another agent filled directly the same morning. Open with Robinhood support.
  Research: Robinhood help "Trading with your agent" says agents place "long equities, options,
  and crypto orders"; "Certain trades may still require your approval even when trade approvals
  are turned off"; approvals "still rolling out for MCP accounts". GitHub chasechipres/rh-gex-trader
  issue #23 (Oct 15 2026, open): same error on a 2-leg MCP order with the setting false. Treat as a
  Robinhood-side limitation: agent multi-leg/short (credit) option orders not accepted for now.
- 2026-11-09: First live entry REJECTED: Robinhood "requires the customer to approve agent-placed
  trades" and multi-leg orders cannot be approved, though get_trade_approval_setting said OFF.
  Never leg a condor in as single orders. Other agent-initiated trades (IWM/SPY single calls)
  happen on this account; the bot must not assume all activity is its own.
- 2026-11-08: Owner put the condor LIVE from 2026-11-09 against the dry-run recommendation:
  backtest at a 15:40 close is +$5.94/trade at model prices but -$2.06 with +1c/leg slippage.
  Watch real slippage per leg closely; > 1c/leg average means the strategy is losing money.
  Bull put spread dropped (needs ~$189 collateral, blocks the condor on its days with $204).
- 2026-11-08: Tradealgo 0DTE article strategies backtested (same model, 09:45 entry, 15:40 close,
  1 lot). Credit spreads/condors 1-2% OTM $2 wide: TP50%/stop rules lose everywhere; hold-to-15:40
  best. Only SPY bull put 2% OTM survives +1c/leg: 126 of 994 days traded, +$2.70/trade, +$340
  total, ~+$1.6/week, worst -$188; collateral ~$189 (fits $200, one loss nearly wipes it).
  Model credit ~$0.10 vs article's $0.30-0.45 examples (model has no put skew).
- 2026-11-08: Backtest re-run (same model, 994 days, 1 lot, fees incl.). Edge is late-day theta:
  avg/trade at +0c / +1c extra slippage per leg: close 15:50 $9.53 / $1.53; 15:45 $7.79 / -$0.21;
  15:40 $5.94 / -$2.06; 15:35 $4.13 / -$3.87; 15:20 -$0.14. +2c loses at every close time.
  Robinhood auto-closes at-risk expiring late-close options (SPY/QQQ) from 15:45 ET, so 15:50 is
  unsafe. Model prices use the day's VIX1D OPEN held flat (no look-ahead, but no skew and no
  intraday IV) -> late-day decay is the least trustworthy part. Go-live needs real quotes to show
  credits/closing debits within ~1c/leg of the model AND a profitable close time <= 15:40.
- 2026-11-08: Setup. Account MCP (••••8852) Options Level 3; buying power $204.29 after
  $120 deposit (enough for SPY + QQQ, ~$130). Trade approvals OFF — must be ON before LIVE.
  Routines run inside one persistent Claude session (org blocks connectors on fresh-session
  routines); these notes are the memory between days, not the chat history.

## Open questions / things to test
- Real vs model: log per leg the real bid/ask at 14:30 and at 15:35/15:40/15:50 to measure slippage
  vs the model's half-spread (0.75% of mid, min $0.01).
- Does the entry routine's start time drift past 14:30 (scheduler showed ~14:34–14:36)?
