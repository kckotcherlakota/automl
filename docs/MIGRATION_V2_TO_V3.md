# Migration: spec v1/"V2" (docs/SPEC_single_leg_0DTE_v1.md) → V3 micro-$200 (docs/MICRO_200_V3_RULEBOOK.md)

Nothing deleted; v1 stays for audit. V3 supersedes the rows below; everything else in v1 (A/B/C
definitions, no-lookahead engine, OPRA data contract, walk-forward folds) is preserved.

| Topic | v1 / V2 | V3 |
|---|---|---|
| Delta band | 0.55–0.65, target 0.60 | **0.20–0.30, prefer 0.25**; outside band → NO_TRADE |
| Spread filter | ≤ min($0.10, 5% of mid) | **≤ $0.03 AND ≤ 15% of mid** |
| Sizing / risk | premium ≤ 0.5% of equity/trade, ≤1%/day | **$40 daily envelope: cumulative filled buy debits + pending reservations + $5 fee contingency ≤ $40 ($35 usable); wins never replenish** |
| Entries | 2/day max, 1 position | same, 2nd only if validated; no forced trade |
| CPI/FOMC/NFP days | skipped entirely | **entry from 10:00 ET**, but PAPER_ONLY unless separate event-day edge |
| Stop | bid ≤ 0.75 × entry | same + **$8 net loss cut** + structure invalidation |
| Target | +1.75R (~+43.75%) | **take profit at net ≥ max($5, 25% of debit); arm trail at max($3, 15%); exit on giveback ≥ max($1.50, 30% of peak)** |
| Time stop | 15 completed 1-min bars | **12** |
| Deadline | flat by 11:45 | liquidate from 11:40, broker-confirmed flat by 11:45 |
| Paper sessions before live | 30 | **60** |
| Holdout gates | PF ≥1.20, DD ≤5%, ≥150 OOS / ≥60 holdout trades | PF ≥1.20, **DD ≤15%, ≥60 OOS / ≥20 holdout trades**, + macro gate ≥20 event-day OOS trades |
| News | calendar embargo only | **daily news service, primary-source actuals + as-of consensus, HOT/COOL/MIXED/INLINE/UNKNOWN; never a direction by itself** |
| Money type | unspecified | BigDecimal |

Conflicting running pieces (to retire/replace): PLAYBOOK §9 paper tracker (old ORB: 0.40 delta,
+40%/−30%, 15:00 exit) → rewritten to V3 Strategy A; PLAYBOOK §10 (v1 premium cap) → V3; condor
routines already disabled. No Java code/tests exist yet, so there is no conflicting code to migrate.
