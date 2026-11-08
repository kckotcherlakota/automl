# Long-term notes

Durable lessons and running statistics. Updated once a week (Friday weekly review) from
`daily_notes.md` before that file is cleared. Read at the start of every trading day.

## Running totals (dry run started 2026-11-09, start equity $100)
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
