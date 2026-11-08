# Long-term notes

Durable lessons and running statistics. Updated once a week (Friday weekly review) from
`daily_notes.md` before that file is cleared. Read at the start of every trading day.

## Setup (routine IDs; all run in session session_012ZPnNbEARMTpMMQ1Weeq33, New York time)
- trig_01R4cdsFSJEjPmRR2157m314  0DTE morning prep (LIVE)  09:07 Mon-Fri
- trig_01U4piD2uwooiZf8YGWNP81z  0DTE condor entry (LIVE)  14:30 Mon-Fri
- trig_01PQpQw3NJam4eySAJ8ZS2f7  0DTE condor close (LIVE)  15:50 Mon-Fri
- trig_0161yiuFh5aixJVJpB5BcwFo  0DTE weekly review           16:17 Fri
- trig_01Y8kAHGdihXxhLSj55KPLCX  daily /clear of this session  17:13 Mon-Fri (chat history is wiped; these notes are the memory)
- Prompts and notes protocol: routine_prompts.md. Mode: LIVE from 2026-11-09 (owner's choice; dry run skipped). Trade approvals were OFF
  on 2026-11-08, so orders execute without a tap in the app.

## Running totals (LIVE from 2026-11-09, start equity $204.29)
| week ending | trades | wins | win % | week P&L (mid) | week P&L (natural) | cumulative P&L (natural) | peak | drawdown |
|---|---|---|---|---|---|---|---|---|

## Real vs model credit
- Model: typical credit $0.35–0.45 per condor, collateral ~$61. (Go-live rule: real credits
  within ~15% of model and total paper P&L not clearly negative.)

## Lessons (keep short, newest first)
- 2026-11-08: Setup. Account MCP (••••8852) Options Level 3; buying power $204.29 after
  $120 deposit (enough for SPY + QQQ, ~$130). Trade approvals OFF — must be ON before LIVE.
  Routines run inside one persistent Claude session (org blocks connectors on fresh-session
  routines); these notes are the memory between days, not the chat history.

## Open questions / things to test
- Does the entry routine's start time drift past 14:30 (scheduler showed ~14:34–14:36)?
