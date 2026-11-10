# Owner build specification v1.0 (received 2026-11-10) — SPY/QQQ long single-leg 0DTE options

Status as given by the owner: **Research / paper trading only.** Stored verbatim in substance
(section numbers and all numeric rules preserved) so every routine and future session can read it.
This file is the authoritative rule source for the single-leg program; PLAYBOOK.md §10 explains how
the running bot applies it.

## Mission and non-negotiables
Auditable, deterministic, event-driven research, backtesting, paper trading and eventually
broker-gated execution for buy-to-open / sell-to-close SPY/QQQ 0DTE calls and puts only. Compare
(A) ORB/VWAP momentum, (B) SMT reversal, (C) hybrid SMT + trend continuation over 1,000 distinct
chronological regular sessions. Select by highest out-of-sample net expectancy subject to drawdown
and robustness constraints, not win rate. Never state profitability before testing on actual
historical executable option quotes.
No short options, spreads, exercise requests, averaging down, martingale, unapproved instruments,
borrowed money, crypto, overnight exposure or trade replication. **No live orders by default:
trading.mode=RESEARCH and live.enabled=false; changing them requires separate deliberate operator
approval and feature gates.** Deterministic, independently testable rules engine; an LLM must not
invent entry signals, alter limits or override a kill switch.

## 1. Fixed study design
- Universe SPY, QQQ; standard 100-share deliverable listed 0DTE calls/puts from point-in-time
  instrument definitions; reject adjusted/mini/nonstandard.
- Hours America/New_York 09:30–16:00 full days. Opening range 09:30–09:45. New entries 09:50–11:30
  only. Liquidate by 11:45. Skip half-days. No overnight.
- Signals from synchronized 1-minute SPY+QQQ OHLCV; completed 5-minute bars for trend regime only.
  Session VWAP = cumulative typical price × volume / cumulative volume on finished bars. 5-min
  EMA(9), EMA(20) with ≥100 prior finished 5-min bars warmup (prior sessions allowed); reset VWAP and
  ORB daily, not EMAs.
- Embargo: skip CPI, FOMC decision and employment-report dates (calendars known before the session);
  separate sensitivity study including them; report excluded dates.
- One instrument at a time; no overlapping SPY/QQQ positions; variants are separate portfolios.

## 2. Common definitions (non-repainting)
- 1-min bar stamped t covers [t, t+60s), known at t+60s; 5-min bar known at its close; act only on
  later quote events after computation and simulated latency.
- Prior rolling levels exclude the current bar. Thresholds in bps of the ETF's own price.
- ORH/ORL = max high/min low of exactly 15 bars 09:30–09:44, frozen at 09:45.
- Bullish SMT (A vs B), same closed 1-min bar: A low ≥2 bps below its prior reference low and closes
  back above it; B's low does not fall ≥1 bp below its own reference low. Bearish mirrors. Evaluate
  both orientations. Reject if both confirm or timestamps misaligned >1 s. Candidate active ≤8 min;
  no stale repeats from the same reference.
- MSS: later 1-min close above the highest high of the 3 completed bars before the sweep (bearish:
  below their lowest low), within 8 completed bars after the sweep and before 11:30.
- Displacement: MSS bar body/(high−low) ≥0.60 and true range ≥1.20 × median TR of prior 20 closed
  1-min bars; zero range invalid.
- FVG: after bullish MSS, first 3-candle pattern with low[i] > high[i−2] (bearish: high[i] < low[i−2]),
  all closed, gap ≥1 bp, forming within 4 bars of MSS; midpoint fixed at formation. Wait ≤5 closed
  bars for a candle trading into the FVG (low ≤ midpoint) and closing above it (mirror for bearish).
  Enter on the next eligible option quote; one attempt; no chase.
- Ambiguous same-bar ordering: never grant the favorable outcome; assume worse or unresolved.

## 3. Strategy A — ORB + VWAP trend continuation
CALL: from 09:50 a closed 5-min SPY or QQQ candle closes ≥ ORH × 1.0003, above VWAP, EMA9(5m) >
EMA20(5m), body/range ≥0.50. Within the next 10 completed 1-min bars, a bar touches ORH ±5 bps,
closes above ORH and above VWAP → CALL after that bar closes; buy at the next tradable ask after
latency checks. No retest in 10 bars = no trade; no second entry from the same breakout.
PUT symmetric at ORL × 0.9997. Invalidation after entry: 1-min close back across ORH/ORL by >2 bps,
or 5-min EMA crossover (after bar close).

## 4. Strategy B — SMT liquidity-sweep reversal
CALL: A sweeps below frozen ORL by ≥2 bps and closes back above; B does not break its ORL by ≥1 bp
(and has not since 09:45) → bullish SMT; then bullish MSS + displacement within 8 min; bullish FVG +
confirmed midpoint retest; buy A's call at the next eligible quote. No VWAP alignment (countertrend
experiment). PUT mirror at ORH. Invalidation: 1-min close beyond the swept extreme by >2 bps or
opposite MSS; abort a pending setup if the sweep extreme is breached again before entry.

## 5. Strategy C — trend-filtered SMT pullback
CALL: A has EMA9>EMA20 (5m) and is above VWAP; bullish SMT using the prior 20 completed 1-min bars'
low (A sweeps ≥2 bps, B fails by ≥1 bp; levels frozen before the sweep; not ORL); bullish MSS,
displacement, FVG, confirmed retest; at confirmation A again above VWAP with EMA9>EMA20; buy A's
call at the next quote. PUT mirror.

## 6. Point-in-time option selection
0DTE chain + contemporaneous NBBO, unadjusted 100-share deliverable; |delta| 0.55–0.65, closest to
0.60, from a timestamped provider estimate or delta computed from current quotes/IV (not VIX1D).
Valid NBBO: bid>0, ask>bid, not crossed/locked, quote age ≤3 s, spread ≤ min($0.10, 5% of mid),
underlying quote age ≤3 s. Prefer lowest relative spread within 0.03 delta of 0.60, then smaller
delta error, then lower premium, then symbol. Buy limit at ask; if unfilled in 5 s cancel and verify;
at most one cancel-replace at the new ask if still valid; abandon after 10 s. Default 1 contract;
size must fit displayed ask size. Exit at realistic bid.

## 7. Exits and risk (same for A/B/C)
- Session-start equity recorded before the open. Max 1 position, max 2 filled entries/day across
  both ETFs, 1 entry per signal, 1 contract per entry.
- **Premium cap: 100 × ask × contracts + fees ≤ 0.5% of session-start equity**; if one contract is
  too expensive, skip (no cheaper OTM substitution).
- Daily gross premium ≤ 1.0% of session-start equity.
- Stop: bid ≤ 0.75 × entry (−25%). Target: bid ≥ entry + 1.75 × (entry − stop) (≈ +43.75%).
- Also exit on strategy invalidation, 15 completed 1-min bars after entry, or by 11:45, whichever
  first; stop has precedence. If quotes vanish: suspend entries, flag, sell when a valid quote
  returns; record realized gap.
- Halts: session loss 0.75% of equity; weekly 2.0%; high-water drawdown ≥5.0% disables live and
  requires manual reassessment. No averaging down, recovery or revenge trades, no silent resets.
- Broker gates: buying power, options level/0DTE eligibility, session, account restrictions; fail
  closed when unknown. Never exercise; never hold through expiration.

## 8. Data contract (summary)
Historical 1-min SPY/QQQ OHLCV (ideally tick quotes); **actual historical OPRA bid/ask (tick or ≤1 s)
for all candidate 0DTE strikes** with definitions and sizes; dated IV/greeks or reproducible ones;
exchange and macro calendars; fees. Raw data immutable with SHA-256 and provenance; Parquet tables.
Most recent 1,000 consecutive full sessions; if unavailable, STOP with a coverage report — never
fill missing option prices from Black-Scholes. ≥99% 1-min coverage 09:30–11:45 each session.

## 9. Backtest engine
Event ordering (event, receive, sequence); decisions after bar close + latency (1 s base; 3 s, 10 s
stress); buy at ask, sell at bid, plus $1/contract/side slippage (2×, 3× stress) and fees; pessimistic
on ambiguity; no-lookahead property tests; full NO-TRADE reason funnel.

## 10. Evaluation
Per-trade ledger and netR with R0 = 100 × contracts × 0.25 × entry + exit costs. Breakdowns by ETF,
side, strategy, period, time, regime, news, fold. Net P&L, avg/median R, win rate, PF, Sharpe,
Sortino, max DD, streaks, tails, costs share, random-entry baseline and ORB ablation; 5-session
block bootstrap ≥10,000; sensitivity to latency, slippage, missing signals, macro days, thresholds,
gap stress; $25k/$50k/$100k equity with the one-contract premium cap.

## 11. Walk-forward and holdout
Sessions 1..1000. Grids: A breakout [2,3,5] bps, retest [5,10] min, body [0.50,0.60]; B sweep [2,4],
MSS lookback [3,5], FVG wait [3,5]; C sweep [2,4], reference [15,20], FVG wait [3,5]. Folds:
train 1–400/validate 401–500/test 501–600; 1–500/501–600/601–700; 1–600/601–700/701–800. Select
one family/config at session 800, freeze, test once on 801–1000. Lexicographic selection: safety and
data gates, then conservative lower-bound net expectancy, then smaller DD, lower cost sensitivity,
fewer parameters.

## 12. Promotion gates (all required before any live consideration)
1. 1,000 valid sessions with option-quote coverage; all timestamp/no-lookahead/reconciliation tests.
2. Walk-forward 501–800 and holdout 801–1000 both positive net mean R; ≥150 OOS trades (≥60 holdout);
   else INSUFFICIENT_EVIDENCE.
3. Holdout PF ≥1.20, holdout max DD ≤5%, ≥2 of 3 folds profitable.
4. Bootstrap 95% lower bound of holdout daily net return >0; beats random-time baseline.
5. Holdout positive under 2× slippage and ≥3 s latency; no cap breaches; gap losses counted.
6. Reproducible replay; immutable reports/config; no unauthorized broker calls; zero overnight.
7. **≥30 live-market paper sessions** with executable shadow fills and clean reconciliation. Paper
   never substitutes real fills. Then only a tiny, manually approved live pilot may be considered.
   No automatic promotion. If nothing passes: NO VERIFIED EDGE; stay disabled.

## 13–17. Architecture, broker integration, operations, deliverables (summary)
Java 17 / Spring Boot modules (market-data, feature-engine, strategy-core, options-selector,
portfolio-risk, backtest-engine, execution-gateway, audit-service, reporting); optional C++ for
features only. Robinhood MCP adapter only via documented, runtime-discovered tools; idempotent
intents, review before place, reconcile orders/positions before retry, sell-to-close at most held,
verify flat, rebuild state from broker truth on restart, HALT on any anomaly, kill switch independent
of the LLM. 09:20 ET preflight; entries blocked at 11:30; flat by 11:45. Repo layout with config/
{research,paper,live-disabled}.yaml, docs/{RULEBOOK,DATA_CONTRACT,NO_LOOKAHEAD,ROBINHOOD_MCP,
SAFETY_RUNBOOK}.md, java/…, research/…, tests/…, reports/…. Milestones: (1) data coverage report with
licensed tick/NBBO for 1,000 sessions — stop if unavailable; (2) deterministic features + no-lookahead
proofs; (3) execution/replay + risk engine; (4) walk-forward + one-time holdout; (5) reports: NO
VERIFIED EDGE or PAPER_CANDIDATE; (6) 30-session paper forward test, then maybe a manual tiny pilot.
Work in small testable commits; first produce an architecture decision record; do not overwrite an
existing rulebook without diff and approval; a task is complete only when provenance, execution,
no-lookahead, reconciliation and walk-forward are reproducible and live trading is disabled.

Primary docs: robinhood.com/us/en/support/articles/{agentic-trading-overview,trading-with-your-agent,
options-trading-hours,basic-options-strategies}; databento.tech/options; theocc.com expiration
pricing considerations; FINRA intraday margin requirements.
