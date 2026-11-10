# Live-readiness report — 2026-11-10 (V3 micro-$200, LIVE_REQUESTED)

Generated 2026-11-10 ~09:00 ET. Config `config/micro-200-v3.yaml`
(sha256 5f6fb833475b99e300689ad9b5a3d9a9c174edf4e790be5adf67c4557b5dfbd4), rulebook
`docs/MICRO_200_V3_RULEBOOK.md` (sha256 82e04102…6c).

**Result: `LIVE_BLOCKED`. live_enabled stays false. Today = MONITOR + PAPER only.**

| # | V3 §15 gate | Status | Evidence / blocker |
|---|---|---|---|
| 1 | 1,000-session real-OPRA backtest, frozen walk-forward + holdout, 60 live-quote paper sessions, for this V3 policy | **FAIL → LIVE_BLOCKED_BACKTEST_OR_PAPER** | No licensed historical OPRA NBBO in this environment (needs e.g. Databento). Only model (Black-Scholes) research exists, which V3 forbids for fills; it was negative for single-leg long options (3,456 configs; ORB long option −$4.99/trade). Paper sessions completed: 1 (2026-11-09, mark-bar replay, not NBBO). |
| 2 | Dedicated MCP account, auth, 0DTE buy-to-open/sell-to-close rights, cash/settlement, automation permission, allowlisted ID | **PARTIAL → LIVE_BLOCKED_BROKER** | OK: account "MCP" ••••8852 is the only agent-tradable account, option_level_3, limited-margin, BP $274.75, no positions, approvals setting OFF (updated 2026-11-09 20:02Z). NOT proven: no single-leg buy-to-open/sell-to-close by THIS bot yet (6 multi-leg orders rejected 2026-11-09 with "requires the customer to approve agent-placed trades"); another agent placed single-leg orders on the same account 2026-11-09 (shared account, not dedicated to this bot). |
| 3 | Licensed fresh NBBO feed, verified news state, service uptime/heartbeat/alerts, operator-validated $40 envelope | **FAIL → LIVE_BLOCKED_DATA_OR_RISK** | Robinhood live quotes only (no OPRA entitlement, no sub-second loop: routines run at most hourly per routine and one at a time in one session — cannot monitor exits quote-by-quote). News: primary BLS verified today (see CPI plan), no licensed Reuters/AP feed. Alerts = Gmail only. No supervised 24h process. |
| 4 | Separate environment release with live_enabled=true, signed operator risk acceptance, policy hash, safety tests | **FAIL → LIVE_BLOCKED_AUTHORIZATION** | None exist. No Java risk/order gateway or tests built yet. |
| 5 | Macro day (CPI) needs separately validated event-day edge (≥20 OOS event-day trades) | **FAIL → PAPER_ONLY_ON_MACRO_DAY** | No event-day evidence at all. |

## Affordability check (V3 §6–7)
$35 usable gross debit (= $40 − $5 contingency) means ask ≤ $0.35 for one contract. 0.20–0.30-delta
SPY/QQQ 0DTE options at 10:00–11:30 have typically cost more than $0.35 (e.g. 2026-11-09 14:36,
0.17-delta 754C ask $0.15 and 0.35-delta 753C $0.38 with 85 min left; in the morning the same deltas
cost several times more). Expect frequent `NO_TRADE: UNAFFORDABLE` — V3 accepts this outcome.

## What runs today
- 09:07 morning prep (read-only) + V3 morning brief: `reports/daily/2026-11-10/morning_brief.md`.
- No live orders. Paper replay of Strategy A under V3 rules after the close (PLAYBOOK §9).
