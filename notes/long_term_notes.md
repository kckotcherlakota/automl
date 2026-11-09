# Long-term notes

Durable lessons and running statistics. Updated once a week (Friday weekly review) from
`daily_notes.md` before that file is cleared. Read at the start of every trading day.

## Setup (routine IDs; all run in session session_012ZPnNbEARMTpMMQ1Weeq33, New York time)
- trig_01R4cdsFSJEjPmRR2157m314  0DTE morning prep (DRY RUN)  09:07 Mon-Fri
- trig_01U4piD2uwooiZf8YGWNP81z  0DTE condor entry (DRY RUN)  14:30 Mon-Fri
- trig_01PQpQw3NJam4eySAJ8ZS2f7  0DTE condor close (DRY RUN)  15:50 Mon-Fri
- trig_0161yiuFh5aixJVJpB5BcwFo  0DTE weekly review           16:17 Fri
- trig_01Y8kAHGdihXxhLSj55KPLCX  daily /clear (DISABLED 2026-11-08: a routine-sent /clear arrives as plain text and does not clear; chat history is compacted automatically, these notes are the memory)
- Prompts and notes protocol: routine_prompts.md. Mode: DRY_RUN (briefly set to LIVE on 2026-11-08, reverted
  the same day before any trade). Trade approvals OFF — must be ON before LIVE.

## Running totals (dry run from 2026-11-09, account $204.29)
| week ending | trades | wins | win % | week P&L (mid) | week P&L (natural) | cumulative P&L (natural) | peak | drawdown |
|---|---|---|---|---|---|---|---|---|

## Real vs model credit
- Model: typical credit $0.35–0.45 per condor, collateral ~$61. (Go-live rule: real credits
  within ~15% of model and total paper P&L not clearly negative.)

## Lessons (keep short, newest first)
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
