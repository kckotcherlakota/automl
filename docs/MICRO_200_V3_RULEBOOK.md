# Claude Code / Robinhood: SPY–QQQ 0DTE micro-account trading system
**Version:** 3.0 | **Request date:** 2026-11-10 | **Starting equity:** $200 | **User-requested launch:** 2026-11-10 | **Operational status:** `LIVE_REQUESTED / NOT_YET_VERIFIED`

> **Priority order:** (1) protect against a loss beyond the $40 daily allowance by reserving the **entire purchase debit**, (2) verify broker/account/data/order safety, (3) evaluate whether 0.20–0.30 delta options are actually affordable and executable, (4) trade only a proven setup, (5) take meaningful executable net profits promptly, (6) learn after the close without automatically rewriting a live strategy. **Do not assume guaranteed profits or guaranteed stop execution.**

This document **supersedes** V2's 0.35–0.65 delta floor, its $10/$20 profile, its blanket CPI-day embargo, and its mandatory one-filled-entry rule. Retire conflicting configuration and tests via an explicit migration diff. Preserve V2's strict deterministic A/B/C signals, historical point-in-time options quotes, no-lookahead backtests, and broker reconciliation.

**Reality check:** A $40 daily loss on a $200 account is **20% of equity**. Full-premium loss is possible even with a 25% stop. This design targets less than $40 of total worst-case *purchase exposure plus fees* each session rather than trusting stop fills. **No profit target, software feature, or strategy guarantees that the account will grow.**

**Live today is a request, not proof of readiness.** The external Claude Code runtime, actual Robinhood MCP agentic account, option permissions, broker order preview/fills, market data, live deployment infrastructure, and independently verified out-of-sample/paper results are NOT observable in this file. `live.enabled=false` until all release gates pass. If those prerequisites have already been independently met and logged today, the operator may authorize a controlled live session. Otherwise launch monitoring + paper mode today and output an actionable `LIVE_BLOCKED` reason list. **Never activate live by changing a boolean alone.**

## 1. Locked scope, trading hours, and news-day exception
- Long SPY/QQQ ETF **buy-to-open** same-day expiration CALL or PUT; **sell-to-close** owned contracts only. Never write options, short, create spreads, exercise intentionally, use borrowed funds, auto-deposit, buy crypto, or hold overnight.
- New entry window on ordinary sessions: **09:50–11:30 ET**. On major 08:30 premarket release days, e.g. **CPI 2026-11-10**, **10:00–11:30 ET only**, after opening-range confirmation. At most one open contract at any moment; max **two broker-confirmed filled entries per session** provided the total daily **gross premium-loss budget** remains available and cash settlement permits. The second trade is NOT loss recovery, is NOT required, and must independently pass the strategy model. If the backtest only validates one entry/day, operate at one entry/day.
- Always attempt exit as soon as any profit-protecting or loss/invalidation trigger fires. Block new entries at 11:30, begin deadline liquidation by 11:40, and target **broker-confirmed flat by 11:45 ET**. If unresolved, continue safe sell-to-close escalation and alert; NEVER mark flat from a request alone. No end-of-day open position.
- 09:30–09:45 ORH/ORL frozen from full regular session. All indicators are calculated from **completed** bars and valid timestamps. During live monitoring consume continuous underlying + option quotes (never assume Claude alone provides a realtime loop). Monitor early closes and unscheduled trading halts.
- **Today-specific scheduled event (2026-11-10):** US BLS October CPI and real earnings scheduled at 08:30 ET. Treasury 10-year auction at 13:00 ET is beyond normal entry window. Query the real official release and scheduled consensus from a point-in-time source; DO NOT prefill the actual value or trade on an unverified headline. Official BLS schedule: https://www.bls.gov/schedule/2026/11_sched_list.htm
- Historical baseline: same 1,000 chronological full regular sessions; pre-register event-day inclusion separately to avoid overfitting. Today's release-day eligibility requires enough event-day **out-of-sample** evidence; otherwise `PAPER_ONLY_ON_MACRO_DAY` even if normal days passed.

## 2. Common definitions (non-repainting)
- A one-minute bar stamped `t` represents `[t, t+60s)` and is **not known until t+60s**. A 5-minute bar is not known until its close. Only act on subsequent quote events after signal computation and simulated system latency.
- Prior rolling levels exclude the current bar. `priorHigh(N, t) = max(high[t-N ... t-1])`, likewise prior low. Tick/price thresholds expressed in bps of that ETF's own underlying price, not raw SPY–QQQ point spreads.
- Opening range ORH/ORL is the max high/min low of exactly 15 bars 09:30–09:44 ET; values freeze at 09:45 ET. The reference for Strategy B is that day's frozen ORH/ORL.
- **Bullish SMT** in ETF A vs ETF B: over the **same closed 1-minute bar**, ETF A's low is at least 2 bps below its prior reference low; its close recovers above that low; ETF B's low does not fall 1 bp or more below **its own** corresponding reference low. **Bearish SMT** mirrors high sweep, close below swept level, and B failing to confirm its own breakout. Evaluate both orientations (A=SPY/B=QQQ, A=QQQ/B=SPY). Reject if both markets confirm the sweep or data timestamps are misaligned >1 second. Record sweep event time and reference levels. Hold candidate active for at most 8 minutes; reject stale repeats from the same reference level.
- **Market structure shift (MSS):** for bullish sweep, close of a later 1-minute bar above highest high of the **three fully completed one-minute bars immediately before the sweep**; for bearish, close below lowest low of those same three bars. This rule is fixed before the sweep and cannot use future pivots. MSS must occur within 8 completed 1-minute bars after sweep and before 11:30 ET.
- **Displacement:** the MSS bar has body / (high-low) >= 0.60 and true range >= 1.20 × median true range of preceding 20 closed 1-minute bars. Handle zero range as invalid.
- **Bullish FVG:** after a bullish MSS, find first 3-candle pattern with `low[current] > high[current-2]` (bearish: `high[current] < low[current-2]`). All three candles must be fully closed and the gap width must be >=1 bp of underlying mid-price. FVG forms within 4 bars of MSS. Its 50% midpoint is fixed at formation. For a bullish setup, wait up to 5 further fully closed 1-minute bars for a candle that trades into the FVG (low <= midpoint) and then **closes above midpoint**; reverse for bearish. Enter on the next eligible options quote, never at the historical midpoint touch. One fill attempt per signal. If FVG does not form/retest in time, skip; do not chase.
- Handle ambiguous same-bar OHLC order by waiting for a **completed retest confirmation** and later quote, not by granting a touch fill. If prices simultaneously meet target/stop in aggregate bars without intrabar options quotes, assume the worse exit or declare the result unresolved; never assign the favorable outcome.

## 3. Strategy A: ORB + VWAP trend continuation
CALL:
1. Starting 09:50, a fully closed 5-minute SPY or QQQ candle closes >= ORH × 1.0003 (3 bp breakout), above session VWAP, with EMA9(5m) > EMA20(5m), and candle body/range >= 0.50.
2. Within the next ten **completed** 1-minute bars, a bar touches ORH ± 5 bps, remains/reclaims above ORH at close, and closes above session VWAP. Signal CALL only after that bar closes.
3. Enter the selected 0DTE call at the next tradable ask after latency checks. No retest within ten bars = no trade. Avoid a second entry from the same ORH breakout.
PUT: symmetric: 5m close <= ORL × 0.9997, below VWAP, EMA9 < EMA20, bearish body, retest of ORL from below, close below ORL and VWAP, then buy put.
Structure invalidation after entry: 1m underlying close back across ORH/ORL by >2 bps in the wrong direction, or 5m trend crossover (only after bar closes).

## 4. Strategy B: SMT liquidity-sweep reversal
CALL:
1. ETF A sweeps **below its frozen ORL** by >=2 bp then closes back above ORL; ETF B does **not** break its own frozen ORL by >=1 bp during that synchronized 1-minute bar (and has not already breached that ORL since 09:45). This is bullish SMT.
2. Require bullish MSS **and** displacement within 8 minutes after sweep.
3. Require a bullish FVG and subsequent confirmed midpoint retest under common definitions.
4. Buy ETF A's 0DTE CALL at the first eligible subsequent quote. Do not require VWAP trend alignment: this is explicitly the countertrend experiment.
PUT: ETF A sweeps and rejects above ORH while ETF B does not confirm, followed by bearish MSS, displacement, bearish FVG and confirmed retest. Buy ETF A PUT.
Invalidation: after entry, 1m close past original swept extreme by >2 bp or opposite confirmed MSS. Abort a pending setup if the sweep extreme is breached again before entry.

## 5. Strategy C: trend-filtered SMT pullback
CALL:
1. The chosen ETF A has EMA9(5m)>EMA20(5m) and at prospective entry is above session VWAP (use only completed 1m/5m observations).
2. A bullish SMT is detected with ETF A sweeping the **prior 20 completed 1m bars' low** by >=2 bp while ETF B fails to sweep its **own** prior-20-bar low by >=1 bp. Both rolling levels are frozen immediately before the sweep. Do not use ORL as the reference here.
3. Require bullish MSS, displacement, bullish FVG, and confirmed midpoint retest as above. At confirmation, ETF A must again be above VWAP and 5m EMA9>EMA20; buy ETF A CALL at the next quote.
PUT: reverse sweep (prior 20-bar high), EMA9<EMA20, below VWAP, bearish MSS/FVG, buy put.
This uses SMT as **trend-pullback confirmation**, not evidence that divergences predict reversals by themselves.

## 6. Point-in-time 0.20–0.30 delta contract selection
**Never treat 0.20/0.30 delta as a predicted 20%/30% profit probability or a profit guarantee.** For a bullish setup buy a CALL; bearish buy a PUT. Use actual point-in-time option instrument definitions, buyable options, current executable NBBO and an as-of delta (prefer timestamped broker/vendor Greek; otherwise compute delta from contemporaneous observed option prices/IV with documented timestamp and assumptions). Absolute delta is in **[0.20,0.30]** inclusive; prefer closest to **0.25** conditional on all safety/edge gates. Never change the band to fit an unaffordable contract. If option pricing makes that band unaffordable, **NO_TRADE**.

For each signal, pull the **entire eligible 0DTE strike chain** in the appropriate right; check:
1. Actual same-day listed expiry, standard 100-share deliverable, no adjusted/mini, and broker permission.
2. Fresh underlying and options quotes (default no older than 3s, reject missing/crossed/locked/bad timestamps), bid > $0 and nonzero bid/ask sizes, and no trading halt.
3. Spread <= **$0.03 absolute AND <= 15% of option midpoint** (both must pass; pre-register 10%, 15%, 20% stress sensitivity). At a $0.20 midpoint, $0.03 is 15%; a $0.05 spread fails. Avoid options whose quoted spread consumes the intended quick profit.
4. Prefer open interest >= 100 and current displayed size >= 1 on both sides; OI may be previous-session stale and must not be treated as current depth; real spread/size/fill odds dominate.
5. Entry limit price computed conservatively from ask and broker-supported valid increments. Reject if max executable debit at that limit + fees exceeds the **remaining full-premium risk budget**.
6. Exclude contracts with implausible/missing delta or extreme implied volatility distortion; check minimum modeled future move needed for net exit and time decay. A cheap option isn't a bargain by itself.
7. If multiple choices remain, rank by **pre-frozen, out-of-sample estimated after-cost net expectancy and live execution quality**; never rank solely by lowest premium, closest strike, headline sentiment, or a language model's confidence. If no validated positive expectancy => `NO_VERIFIED_EDGE`.

Research 0.20–0.25 and 0.25–0.30 as distinct ablations; delta is a *selection filter*, not an entry signal. Record delta at signal, order review, and fill; detect stale delta/drift from large underlying moves. Hold actual 1-contract max.

## 7. $200 account, daily maximum loss $40, prompt profit exits
### 7.1 One authoritative daily-loss envelope
- Freeze `sessionOpenEquity`, day-start settled cash, account type, pending orders, and high-water drawdown based on broker data. `daily_loss_limit_usd=40.00` **including real fees/known charges**, enforced prospectively by capped cumulative **gross purchase exposure** plus fee contingency, with no positive daily profit cap. Losing $40 equals 20% of the starting account and is not a safe risk fraction; display a visible warning and allow operator to request a smaller limit. Do not automatically increase it with growing equity.
- Track `cumulativeFilledEntryDebitsToday = sum(full debit of every broker-confirmed buy-to-open filled today)`, **including closed winning trades**, and `pendingReservedEntryDebitsToday = sum(max potential debit of outstanding buy-to-open intents not yet filled)`. On new entry enforce **`cumulativeFilledEntryDebitsToday + pendingReservedEntryDebitsToday + candidateFullBuyDebit + feeContingency <= $40`**. This is a conservative **gross-at-risk spend ceiling**; profits never replenish it. Log `grossRealizedLossToday=sum(max(0,-netPnL_i))` separately for monitoring, but **do not add it again** to cumulative entry debits or losses would be double-counted. Move a fill atomically from pending reservation into cumulative debits using the persistent order ID; partial fill risk is reserved once.
- **Default prudent cushion:** `feeContingency=$5.00 per session`, giving **$35 usable aggregate filled-entry and pending-entry purchase budget** initially, including known buy-side fees. Previous filled purchases consume it permanently for that session whether sold for a gain or loss. If actual fees or inability to close can exceed the cushion, increase contingency or block; it is not a promise losses cannot exceed $40 if the broker charges unexpected fees or account holdings diverge. Broker-approved purchases must have `candidateFullBuyDebit + reserve <= remainingBudget`. $0.35 ask (100 shares) costs $35 before fees; if the contingent $5 plus actual entry/exit fees push risk above $40, reject. $0.40 ask costs $40 before fees and **fails** the default envelope.
- Never assume stop fills cap risk. A long 0DTE option can fall instantly to zero. No averaging down, doubling, revenge entry, extra size to recover, or debt/margin. At most one position at a time. Cumulative daily gross debit budget is **not reset** after profits; total full-premium exposure and losses remain bounded by this day's frozen allowance.
- Session current realized+conservative mark loss at or beyond $40 => halt openings, prioritize safe close/reconciliation. Maintain separate actual `dailyNetPnL` and `grossRealizedLossToday`; neither permits spending beyond the locked cumulative $35 purchase budget. If losses exceed $40 due to operational defects, alert immediately; don't continue trading to earn it back. No code can guarantee a strict $40 realized-loss ceiling under exchange/broker outages or fees outside the budget.
- Check weekly/monthly and account-wide drawdown alarms separately (monitoring/warnings, no automated removal). A 20% drawdown requires manual authorization for any later day's new live session; **never let a daily $40 cap hide severe cumulative losses**.
- Broker-confirmed available settled cash must cover purchase/known fees + $5 unspent cash reserve. Cash-account proceeds may not be spent again until actual settlement; check broker's account restrictions and day-trading permissions.

### 7.2 Sell early when net executable profit is meaningful
Run quote-driven exit evaluation (fresh actual executable BID, not mid, theoretical mark or displayed last price). Compute `liquidationNetPnL = (executableBid * 100) - actualEntryDebit - estimatedExitFees - adverseExecutionCushion` for one contract. `actualEntryDebit` includes purchase and entry charges. Configure **comparison experiments**, freeze the selected policy out-of-sample and in live config; the initial paper candidate below is NOT proven optimal:
- `early_profit_arm`: if net executable profit >= `max($3.00, 0.15 * actualEntryDebit)`, arm profit-protection trailing logic. Track peak **executable net profit** (not unrealized last trade).
- `take_profit_now`: as soon as net executable profit >= `max($5.00, 0.25 * actualEntryDebit)`, immediately submit supported **sell-to-close** marketable limit *with price/depth safeguards*, for entire owned quantity. No minimum hold time if already filled and profit is real. A sell order can still fail to fill. The limit must not sell below a preconfigured slippage guard without operator exception; do not indefinitely wait and give back the gain.
- `protect_winner`: after early-profit arm, if net executable profit gives back >= `max($1.50, 0.30 * peakExecutableNetProfit)` from its peak, initiate sell-to-close if still positive; otherwise standard risk exit applies. Do not move stop lower to widen risk.
- `fast_loss_cut`: provisional `bid <= 0.75 * entryFillPrice` OR underlying structure invalidation OR observed net loss >= `$8` on a trade, whichever first. This is an **exit attempt**, not a guaranteed loss cap; a $35 debit can still go to zero. Optimize stop parameters only on training/validation as pre-registered grid [-20%, -25%, -35%]; exit slippage and quote flicker must be modelled.
- `time_exit`: after 12 completed one-minute bars from entry if neither target nor stop triggered; close sooner upon 5-minute trend invalidation. No trading on stale prices or in an unresolved order state. Preserve time-vs-requote order prioritization and show fill reconciliation.
- `deadline`: no entries after 11:30; force safe liquidation from 11:40; broker-confirmed flat goal 11:45. If a position persists, repeated unsafe unverified sell orders are forbidden; escalate and use documented cancel/replace with broker-held position proof.

**Illustrative 1-contract examples (before fees):** buy at $0.20 = $20 debit; sell at $0.25 = $25 => $5 gross profit; sell at $0.15 => $5 gross loss; worthless => $20 gross loss. Buy at $0.35 = $35 debit; sell at $0.40 = $40 => $5 gross profit; worthless => $35 loss. Spread, commissions/fees, and fill slippage reduce realized results. Keep a NO_TRADE branch rather than buying deeply OTM contracts just because they fit the account.

## 8. Data acquisition and quality contract
Must acquire/licence:
1. SPY/QQQ **historical 1m OHLCV and preferably underlying tick quotes**, adjusted consistently for underlying corporate actions; data timestamps in UTC, session calendar in NY timezone.
2. **Actual historical OPRA bid/ask quotes (ideally tick/NBBO events or <=1 second samples)** for all eligible 0DTE candidate strikes; contemporaneous option definitions, expiry dates, strike, call/put, multiplier, quote sizes, and listing/delisting. Avoid survivor-only chains. One-minute option OHLC is insufficient for high-quality execution simulation.
3. If Greeks are unavailable point-in-time, compute reproducible deltas from a properly dated implied-volatility estimate **derived from that option's contemporaneous NBBO**, not from a later end-of-day surface; sensitivity-test delta error. Do not substitute Black-Scholes theoretical premiums for observed fills.
4. Historic official US exchange calendars, scheduled CPI/FOMC/employment releases as known before session, and any transaction fees/regulatory fees applicable in the simulated account and period.
5. Store raw vendor responses immutable with SHA-256 hashes, time ranges, provenance, schema version, UTC timestamp, instrument identifiers and checksums. Normalize to Parquet with separate contract-definitions, underlying-bars, quote-events, calendars, macro-events tables.
6. Select the most recent **1,000 consecutive full regular sessions** satisfying baseline data availability, preferably ending at the latest fully completed vendor-provided session, but do not cherry-pick sessions by performance. Also reproduce the full research with **$200 starting equity and real premium affordability gates**; track daily declined entry opportunities and settled-cash accounting. If gaps make this impossible, STOP with a coverage report instead of filling unknown option quotes or silently excluding losses. Flag missing data and report both calendar coverage and candidate-level fill coverage.
7. Baseline quality: all 1,000 sessions must have >=99% 1m underlying bar coverage during 09:30–11:45, and each simulated order/exit must have defensible contemporaneous option NBBO or a transparent conservative failure model. Abort on unresolved sequencing, duplicate, corrected/cancelled, crossed quote or daylight-saving defects.

## 9. Backtest event engine: no lookahead
- Strictly sorted `(event_timestamp, receive_timestamp, source_sequence)` with deterministic replay; events carry both market and receive timestamps. Source reception lag must be reflected in the simulation.
- All decisions occur **after** relevant bar close, after feature calculation, plus explicit configurable computation/network/order latency (base 1 second, stress 3 and 10 seconds). Only next available quote after allowed timestamp can become a fill candidate.
- Buy at **ask**, sell at **bid**, then add fees and adverse slippage. Do **not** buy/sell at mid, at OHLC candle high/low, at unobserved FVG price or at future option Greeks. For size>displayed quantity, require depth model or prohibit trade.
- Base-case added slippage: 1 cent per contract-share unit on each side (i.e. $1/contract/side) on top of quote spread, rounded to valid price increments. Stress cases: 2x and 3x added slippage and latency. Include fees separately; values are assumptions, not historical broker-fill measurements.
- If limit price cannot realistically execute, simulate nonfill. If ambiguous, be pessimistic. Handle asynchronous order submission, partial fills, timeout, cancel race, order amendment and reconciliation; do not infer execution from a quote touch.
- Require no-lookahead property tests: perturb all **future** prices/quotes and verify earlier decisions unchanged; verify exactly which event made a signal knowable; unit test timezone/DST/half-day; explicit same-bar stop/target ambiguity test.
- Persist every NO TRADE reason and signal funnel (sweeps -> MSS -> FVG -> quote eligible -> attempted -> filled -> exited), including missed trades caused by capital constraints.

## 10. Pre-registered research objectives and metrics
Primary objective: maximize **lower confidence bound** of portfolio net daily return and net expectancy per real executed trade, subject to max drawdown, risk-budget compliance, affordable strike availability, executable fills and operational correctness. Include no-trade sessions in portfolio returns. Report A/B/C separately: net dollars, net R (normalized by initial planned stop, plus dollar/full-premium tails), profit factor, 95% CI and sample size, win rate, avg win/loss, drawdown $/% from $200, loss streak, percent $40 daily-budget near-breaches, cash settlement utilization, spread/fees/slippage fraction of gross profits, signed market/news labels, and fill rejection reasons.

**Compare at equal budget:** Strategy A ORB+VWAP vs B SMT reversal vs C SMT+trend, **all in the same 1,000 historical sessions**, with 0.20–0.30 deltas, ≤$40 net exposure envelope (default $35 premium cushion), exact early take-profit and stop candidate policy, maximum 2 entries/day and one position. Add ablations: without news filter, with news filter, 1 vs 2 trades/day, take-profit [net >= max($3,15%), >= max($5,25%), >= max($8,40%)], early profit-arm trail [25%,30%,40% peak giveback], and stop [20%,25%,35% premium]. Candidate policy selection happens only in training/validation, never based on held-out outcomes. Report data-mining multiple-comparison risk explicitly.

Backtest any news reaction rules using **historically point-in-time forecasts, actual releases with publication timestamps, and media publication times**; do not assign surprise labels or future article conclusions retroactively. Stress 1/3/10-second latency, 1/2/3-cent incremental slippage per share each side, crossed/locked/zero bids, news IV crush, 2x/3x spread, partial/nonfills, 100% premium loss, and broker outages. Include **effective transaction cost break-even**, i.e. how many $5 winners are required to offset a $25 or $35 tail loss. Do not exclude no-signal or unaffordable days to inflate returns.

## 11. Locked 1,000-session walk-forward evaluation, no peeking
- Use exactly **1,000 consecutive full regular trading sessions**, as-of exchange calendar with underlying bars, actual option NBBO, contemporaneous contract chain, as-of Greek estimates and source vintage for news. Audit missing coverage rather than making up data. For live candidate include any latest **completed** sessions as available from vendor, never require unavailable future data.
- Session 1–400 training; 401–500 validation; 501–600 frozen forward test. Retrain 1–500/validate 501–600/test 601–700. Retrain 1–600/validate 601–700/test 701–800. Freeze selected family/params at 800; 801–1000 final untouched holdout inspected **once**. Only test days become eligible for future retrains *after* their chronological execution. Use calendar-inference/no lookahead tests for macro forecasts vs actual and live feed latencies.
- Use explicit selection: all risk/data gates pass -> highest conservative 95% block-bootstrap lower confidence bound of out-of-sample net daily expectancy -> lower drawdown -> lower cost sensitivity -> fewer tuneable parameters. Do not select winning family after examining the final holdout.
- Launch gates: **all** timestamp/no-lookahead and broker-mock tests pass; 1,000 sessions and defensible historical NBBO; 501–800 and 801–1000 positive after all costs; ≥60 out-of-sample $200-affordable filled trades, ≥20 final holdout; final profit factor ≥1.20 and max drawdown ≤15%; positive 95% block-bootstrap lower bound for holdout daily net expected return; positive under 2x slippage/≥3s latency; no worst-case daily risk-budget breaches; ≥60 real-time paper sessions and no unresolved reconciliation incident. These are intentionally demanding **promotion conditions, not established empirical results**.
- Macro-event live gate is **independent**. If high-impact event sessions in test+paper do not have enough separate valid observations (pre-register minimum ≥20 event-day trades out-of-sample, otherwise underpowered), permit macro day **paper monitoring only** even if normal-day strategy is accepted. A macro-day parameter change requires a new research version and fresh future holdout. If no strategy passes, output `NO VERIFIED EDGE`; stop trying parameter combinations until one passes by chance.

## 12. News intelligence + market-regime daily planner (not an autonomous prediction engine)
**News service runs every day regardless of trade activity**, including weekends for next-session preview. Implement a fast, independently verified pipeline:
1. `06:45 ET`: recover broker state, fetch official BLS/BEA/Federal Reserve/Treasury calendars; compare known timestamped events with yesterday's saved schedule, mark changed/revised items; confirm trading holiday/half-day.
2. `07:00–08:25 ET`: ingest timestamped Reuters/AP/recognized primary-source market news (vendor access/license required), US index futures, rates, Treasury yields, VIX/VIX1D, SPY/QQQ premarket, relevant large-cap earnings; deduplicate/score by **scheduled release, verified issuer primary statement, surprise magnitude, source trust, affected symbols, timing**, not opinionated bullish/bearish LLM narrative. Output links, actual timestamps, `KNOWN` vs `UNCONFIRMED`, and how old the information is. If unavailable, news-dependent trading is disabled.
3. `08:30/other scheduled release`: never trade a forecast. Verify **actual** with primary source (e.g. BLS for CPI) + one timestamped trusted cross-check; retrieve historical *consensus as published before release* and prior values/revisions. Classify: `HOT` if both available headline and core MoM surprises >= +0.1 percentage point, `COOL` if both <= −0.1 pp, `MIXED` if opposite/no agreement, `INLINE` otherwise. If key data is missing/incomparable/late, `UNKNOWN` and no automated news directional override. Record full source/times. For event types besides CPI, write event-specific features and thresholds validated on prior data; do not force CPI thresholds onto jobs/FOMC.
4. `09:30–09:45 ET`: observe completed opening range and VWAP; after confirmed news release, calculate 5m closes, breadth/regime where available, QQQ–SPY concordance/SMT divergence and option spread/IV. Separate market confirmation from event interpretation. For today's CPI, defer first entry until **10:00 ET** and skip if initial move is chaotic, spreads widen, data stale or trends conflict. A bullish news interpretation never independently generates a CALL; bearish never independently generates a PUT.
5. `news_regime` enum: `NORMAL`, `PREMARKET_HIGH_IMPACT_CONFIRMED`, `INTRADAY_HIGH_IMPACT_PENDING`, `POST_RELEASE_STABILIZED`, `WHIPSAW_UNTRADEABLE`, `UNKNOWN`. Freeze state per event and audit transitions. For scheduled intraday high-impact event, prohibit new entries from **15 min before through 20 min after**; reopen only with completed 5m-bar confirmation and validated normal/event-specific model.
6. Strategy gating: confirmed sustained uptrend -> evaluate CALL from A or C if selected/validated; sustained downtrend -> evaluate PUT from A/C; sweep+nonconfirmation+MSS/FVG -> B only if B has separately established edge; mixed/chop/high spread -> **NO_TRADE**. Never hardcode CPI surprise to call/put direction or equate 'news outcome' with guaranteed returns. On macro days, apply separately frozen validated strategy family; absent validation => PAPER_ONLY.
7. `10:50/13:00/15:30 ET`: refresh schedule for Fed speeches, Treasury auctions, surprise headlines and after-entry volatility. Risk engine should never increase size or reverse a trade based solely on headlines. Even after trading ends, news updates affect only the after-close intelligence report and tomorrow's watch list.
8. `16:15–18:00 ET`: record market close vs morning thesis, realized surprise, time-stamped news, price reaction and tradability. Quantify **regime-classification accuracy** without labeling a trade correct because the market eventually moved in the anticipated direction; time-to-signal/fill and net P&L matter.

**Today's machine-readable event seed** (verify at runtime): `2026-11-10 08:30 America/New_York | BLS October CPI/real earnings | HIGH_IMPACT | actual=UNVERIFIED | direction=NONE | special entry start=10:00 ET`.

## 13. End-to-end morning, trade, and after-close runbook (all America/New_York)
| Time | Mandatory autonomous task | Outputs/failure behavior |
|---|---|---|
| 06:45 | Load persisted state; reconcile prior positions/orders and cash settlement; verify health, credentials, system clock, allowed broker account | `BROKER_RECONCILIATION_OK` or `HALT` |
| 07:00 | Read original economic calendar, releases, speeches, futures/rates and top trusted news; preserve timestamps | `daily_news_brief.json`, source links; unknown -> disable news play |
| 07:45 | Compare macro forecast vs data availability, create scenario tree for hot/cool/mixed/in-line and likely *trade restrictions* | `daily-plan.md` (no fixed CALL/PUT before price confirms) |
| 08:30 | Ingest scheduled release, verify actual and consensus vintage, update release status; today: CPI | `UNVERIFIED` until trusted sources agree; log any changed schedule |
| 09:00 | Fresh SPY/QQQ quotes/OPRA, options approval and $40 ledger check, permitted model hash, strategy proof, connection tests | `READY_MONITOR_ONLY` or `HALTED` if prerequisites fail |
| 09:30–09:45 | Collect 15m opening range and 1m/5m indicators; watch but do not enter | Frozen ORH/ORL and time-stamped features |
| 09:45–10:00 today | Confirm post-CPI stabilization, market structure, option chain delta and spread; NO orders | Market regime and `TRADE_ELIGIBLE` only if vetted event-day mode |
| 10:00–11:30 today | Run selected deterministic strategy; choose 0.20–0.30 delta BUY call/put if real edge and risk budget. Monitor executable bid continuously while position held | Full signal, news gate, quote, order-intent, risk and fill audit |
| At any sell trigger | Take profit promptly if realized-executable threshold; or structural invalidation, stop, time, market-news risk, quote-loss/kill switch | Sell-to-close only confirmed held quantity; reconcile status before retry |
| 11:30 | Disable opening intents | `ENTRY_DISABLED` persisted |
| 11:40–11:45 | Deadline exit + order/position reconciliation | Confirmed flat or immediate high-priority human alert; continue safe reconciliation |
| 12:00 | Post-trade inspection, net proceeds, actual fee and slippage accounting, settle pending flags, no revenge-trade | `trade_review.json`, exceptions and remaining daily risk |
| 16:15 | Independent end-of-day broker confirmation: no open 0DTE options, orders, exercise risk or unintended ETF shares; confirm cash/buying power | Critical incident if nonflat/unknown |
| 16:30–18:00 | Research replay/scorecard; compare news forecast vs realized reaction; portfolio net P&L; create next-session plan and notifications | `daily-report.md`, `next-day-brief.md`, immutable ledger |

Even on `NO_TRADE` days, do news reading, reconciliations, after-close reporting and feedback calculations. Automate the scheduling via real OS/runtime cron/queue with a supervised reliable process; Claude Code alone is not a guaranteed 24-hour monitoring service. Restart must reconstruct broker truth before ANY new order.

## 14. Implementation architecture (Java 17 / Spring Boot; C++ optional)
**One authoritative risk/order gateway**, fed by pure deterministic signals. Suggested modules:
- `market-data` vendor/OPRA adapters, 1m+5m closed-bar builder, as-of Greek/quote QC and UTC↔ET/DST handling;
- `news-service` official calendars, actuals, consensus-vintage storage, news source ingestion + dedup, provenance, `NewsEventState`, `MarketRegimeGate`, premarket/day-close briefs;
- `strategy-core` immutable A/B/C signal finite-state machines (ORB/VWAP, synchronized SMT sweep, MSS, FVG and retest);
- `options-selector` 0.20–0.30 delta eligible chain, whole-contract affordability, spread/order size/fee/liquidity filters;
- `risk-engine` `DailyRiskLedger` tracking cumulative filled buy debits + pending option debit reservations + fees cushion ≤$40; actual realized net/gross losses separately, cash settlement, position max1, freeze periods, 2-entry max, account drawdown warnings;
- `execution-gateway` broker preview, timeboxed limit BUY_TO_OPEN/SELL_TO_CLOSE, real-time quote monitors, reconciler, idempotency, manual override controls and kill switch;
- `research` historical options NBBO replay, news event timestamp playback, paired A/B/C 1000-session walk-forward, stress, bootstrap, frozen holdout;
- `audit-and-reports` immutable inputs/config hash, order/fill event ledger, morning news brief, after-close report, drift flags, human-readable incidents.

Components are designed around dependency inversion (`BrokerAdapter`, `HistoricalQuoteProvider`, `EventCalendarProvider`, `NewsSource`, `Clock`) and testable without Claude/broker. Use Java BigDecimal for USD budget ledger (no float currency), order states (`NONE/INTENT_RESERVED/SUBMITTED/PARTIALLY_FILLED/FILLED/EXITING/CLOSED/UNKNOWN`), atomic DB transaction to reserve premium before order review, and broker refresh idempotency. Consider C++ **only** after profiler evidence; never use a separate independent CPP trading/risk path. Persist events for failover.

## 15. Robinhood official MCP connection, live request, safety gating
Official documented connection for Claude Code:

```bash
claude mcp add robinhood-trading --transport http https://agent.robinhood.com/mcp/trading
# Then, inside Claude Code:
# /mcp   -> authenticate robinhood-trading and complete dedicated MCP account onboarding
```

Robinhood says an external agent can access account/positions, options tools and trade only in a **dedicated Robinhood agentic/MCP account**. Trade approvals settings and tools can vary. Discover exact supported tool schema, order types, options approval, 0DTE permission, account type, intraday limitations, cash buying power, settlement, order review, and owned-quantity sell-to-close support at runtime. **Do not invent tools, circumvent approval, scrape Robinhood, or call undocumented private APIs**. Official docs: https://robinhood.com/us/en/support/articles/agentic-trading-overview/ and https://robinhood.com/us/en/support/articles/trading-with-your-agent/.

`LIVE_REQUESTED` means the operator wants a live launch today, but **nothing in this file authorizes blind orders**. The model cannot switch from paper to LIVE on its own or skip any of these independent acceptance tests:
1. The specified 1,000-session actual option-quote backtest, heldout evaluation and 60 live quote-based paper sessions have already passed and are independently reproducible for **this V3 delta/news/profit policy**; macros have *separate* evidence. If not, `LIVE_BLOCKED_BACKTEST_OR_PAPER`.
2. Dedicated Robinhood MCP account exists, user authenticated, 0DTE buy-to-open/close rights confirmed, cash/settled funds and day-trading restrictions checked, appropriate automation permission confirmed, actual account ID allowlisted; fail => `LIVE_BLOCKED_BROKER`.
3. Vendor feed/OPRA entitlement proves fresh executable NBBO, news official release state known, trading service uptime/heartbeat/order-incident emergency alerts configured, and operator has inspected today's scenario and validated the $40 cash exposure envelope; fail => `LIVE_BLOCKED_DATA_OR_RISK`.
4. Explicit **separate environment configuration release** with `live_enabled=true`, a signed operator risk acceptance document, risk-policy hash and unit/integration safety tests. If any missing => `LIVE_BLOCKED_AUTHORIZATION`. Do not infer approval solely from 'live today' message, as it does not verify technical readiness.
5. On today's CPI release, if the **separately validated** high-impact macro policy has insufficient data, keep macro-day LIVE disabled even if normal-day entry tests pass; produce paper-only signals. Do not mislabel an untested CPI-day trade as safe.

If all five do pass **before the session's allowed entry window**, allow a max one-contract controlled LIVE session under documented broker support, with exit and reconciliation guards. If not, run the news planner and shadow-paper service today and report the precise missing gate. Never place orders in the standard account accidentally. The agent must be supervised; it cannot guarantee that its automatic stop or daily budget protects against unexpected brokerage fees, outages or exercise risk.

Robinhood warns it may attempt to close at-risk expiring ETF options starting 15:30 ET, and that expiration/exercise may cause share deliveries. This system deliberately closes much earlier. References: https://robinhood.com/us/en/support/articles/options-trading-hours/ and https://robinhood.com/us/en/support/articles/expiration-exercise-and-assignment/.

## 16. Suggested versioned configuration (deploy as paper until live release gates)
```yaml
version: '3.0'
account:
  starting_equity_usd: 200.00
  broker: robinhood_trading_mcp
  require_verified_agentic_account: true
  require_settled_cash_if_cash_account: true
  cash_reserve_usd: 5.00
trading:
  mode: PAPER                     # LIVE_REQUESTED does not mean LIVE_ENABLED
  live_requested_date: '2026-11-10'
  live_enabled: false
  require_independent_live_release: true
  symbols: [SPY, QQQ]
  long_single_leg_0dte_only: true
  max_open_contracts: 1
  max_filled_entries_per_day: 2
  allow_second_trade_only_if_validated: true
  timezone: America/New_York
  regular_entry_start: '09:50'
  high_impact_0830_entry_start: '10:00'
  entry_end: '11:30'
  exit_deadline_start: '11:40'
  broker_confirmed_flat_target: '11:45'
  no_forced_trade: true
risk:
  hard_daily_loss_allowance_usd: 40.00
  loss_budget_method: CUMULATIVE_FILLED_BUY_DEBITS_PLUS_PENDING_RESERVED_BUY_DEBITS
  do_not_replenish_spend_budget_after_any_exit: true
  default_session_fee_contingency_usd: 5.00
  gross_debit_budget_before_prior_losses_usd: 35.00
  apply_account_drawdown_halts: true
  no_margin_or_borrowing: true
  no_averaging_down: true
options:
  preferred_abs_delta: 0.25
  min_abs_delta: 0.20
  max_abs_delta: 0.30
  max_spread_abs_usd: 0.03
  max_spread_mid_ratio: 0.15
  max_quote_age_seconds: 3
  require_actual_fresh_nbbo_and_liquid_bid: true
  normal_multiplier: 100
exits:
  early_profit_arm_net_usd: 3.00
  early_profit_arm_fraction_of_entry_debit: 0.15
  immediate_take_profit_net_usd: 5.00
  immediate_take_profit_fraction_of_entry_debit: 0.25
  trailing_giveback_net_usd: 1.50
  trailing_giveback_fraction_peak: 0.30
  attempted_stop_pct_option_premium: 0.25
  attempted_loss_cut_usd: 8.00
  time_stop_completed_one_minute_bars: 12
  profit_calculated_at_actual_bid_net_of_all_costs: true
news:
  enabled: true
  require_primary_source_actuals_and_asof_consensus: true
  major_releases: [CPI, PPI, NFP, FOMC, PCE, GDP, RETAIL_SALES]
  today_high_impact_event: CPI
  today_0830_release_verified: false   # set only after real source verification
  no_pre_release_directional_guess: true
  no_trade_on_unknown_news_state: true
  intraday_event_blackout_before_minutes: 15
  intraday_event_blackout_after_minutes: 20
  require_separate_high_impact_out_of_sample_evidence: true
research:
  strategies: [ORB_VWAP, SMT_REVERSAL, SMT_TREND_HYBRID]
  chronological_sessions: 1000
  require_real_option_nbbo: true
  require_frozen_walkforward_and_holdout: true
  forward_paper_sessions: 60
safety:
  broker_reconcile_before_entry_and_after_exit: true
  require_exit_fill_confirmation: true
  unknown_order_fail_closed: true
  complete_trade_ledger: true
  no_automatic_model_retraining_to_live: true
  no_automatic_increase_to_40_dollar_limit: true
  no_orders_when_event_day_not_validated: true
```
**Configuration consistency:** the `$35` number is **derived** from `$40 − $5 contingency`, not an independently permitted daily loss limit. The risk engine should derive it at run time so it cannot drift from the ledger. The examples above are initial hypotheses for paper evaluation, not a guarantee that any 20–30 delta SPY/QQQ option falls under $0.35 ask or has adequate spread/depth.

## 17. Daily risk+news decision flow (implementation pseudocode)
```text
ON BOOT / 06:45 ET:
  assert calendar, clock, risk-policy-hash, feature flags, audit-write, alert-health
  b = broker.reconcile_accounts_orders_positions_balances()
  if b.unknown or b.unapproved or b.stale: HALT_AND_NOTIFY
  freeze morning_equity, settlement and $40 max gross-loss budget
  fetch official calendar + trusted news; snapshot provenance, timestamps, surprises
  establish trading day (regular/major_event/holiday/half_day)
  if live requested and !allReleaseGates: mode=PAPER; publish LIVE_BLOCKED(reason)

ON CPI RELEASE / OTHER MAJOR EVENT:
  actual = fetch_primary_source_time_stamped()
  forecast = fetch_pre_release_consensus_vintage()
  if unverified or incompatible: newsState=UNKNOWN; DISABLE_EVENT_TRADE
  classify surprise; NEVER infer buy CALL/PUT from surprise alone

ON COMPLETED BAR:
  if event blackout OR outside approved entry time: RETURN
  if unknown broker order/position OR exposure/risk ledger untrusted: HALT_AND_NOTIFY
  if event day without independent proven event-day edge: PAPER_ONLY
  regime = validated_price_and_news_confirmation()
  signal = frozenSelectedStrategy.evaluate_only_known_bars(regime)
  if signal empty: record NO_SIGNAL; RETURN
  chain = point_in_time_option_quotes_and_delta(absDelta=0.20..0.30)
  candidates = filter(chain, freshNBBO, spread, size, expiry=today,
                      ownedType=longCallOrPut, feePreview, cashSettlement)
  riskAvail = 40.00 - cumulativeFilledEntryDebitsToday
                    - pendingReservedEntryDebitsToday - feeContingency
  candidates = filter(candidates, fullBuyDebit <= riskAvail)
  if none OR no validated positive net expectancy: record NO_TRADE; RETURN
  if filledEntriesToday >= validatedDailyMax: RETURN
  reserve fullBuyDebit atomically; broker.review_order()
  submit documented idempotent limit BUY_TO_OPEN only if authorized
  reconcile actual fill; do not re-submit ambiguous request blindly

ON EVERY FRESH OPTION QUOTE WHILE OPEN:
  netIfLiquidated = validBid*100 - actualBuyDebit - projectedExitFees
                   - executionCushion
  if netIfLiquidated >= max(5, 0.25*actualBuyDebit): SELL_TO_CLOSE_NOW
  else if netIfLiquidated >= max(3, 0.15*actualBuyDebit):
       armTrail; peak = max(peak, netIfLiquidated)
  if armedTrail and peak-netIfLiquidated >= max(1.50, 0.30*peak):
       SELL_TO_CLOSE_NOW
  if bid <= .75*entryFillPrice OR actualNetLoss >= 8
       OR structureInvalidated OR 12_barTimeStop: SELL_TO_CLOSE_NOW
  if at_or_after_11_40_ET: SELL_TO_CLOSE_DEADLINE
  for any sell action: quantity<=broker_confirmed_held; reconcile terminal fill
  if unknown exit order: HALT_ENTRIES; resolve broker truth; escalate

AFTER LAST TRADE AND AFTER 16:15 ET:
  reconcile broker positions/orders/cash/unsettled proceeds/fees
  ledger cumulativeFilledEntryDebitsToday, grossRealizedLossToday, netPnL,
         drawdown, profit capture, slippage
  compare planned release scenario with true postrelease market behavior
  record wins, losses, false breakout, spread, IV crush, faults separately
  produce source-linked daily summary and next-day calendar
  propose research changes only; NO silent auto-deployment
```

## 18. Deliverables, tests, and explicit acceptance
**Create:** `config/micro-200-v3.yaml`, `docs/MICRO_200_V3_RULEBOOK.md`, `docs/NEWS_INTELLIGENCE_AND_EVENT_GATES.md`, `docs/2026-11-10_CPI_PLAN.md`, `docs/ROBINHOOD_MCP_CAPABILITIES.md`, `docs/LIVE_LAUNCH_CHECKLIST.md`, `docs/MIGRATION_V2_TO_V3.md`, `reports/daily/{date}/morning_brief.md`, `news_events.jsonl`, `trades.csv`, `daily_report.md`, full backtest/fold/holdout reports, and tests. Do not overwrite earlier files without audit history.

**Critical tests:**
1. Initial $200, delta .25, ask $0.25, all fees $0 => $25 full debit, $5 contingency, residual budget $10. Two trades of $25 each on same day **fail** even if first made a profit, because cumulative gross buy debits policy cannot recycle wins to fund >$35 gross exposure.
2. First trade $25 debit loses $7 including fees; remaining gross purchase budget is **$10** under the starting $35 limit, irrespective of the exit's $7 loss. A second $30 debit order **fails**; a second $10 debit may pass only if all other gates, actual fees and cash settlement permit. Count each filled entry debit **once**, preserve pending reservations, and do not replenish the budget even after a winner.
3. First trade bought $20, exits for $25 before fees: $5 gross gain, but profit trigger sees **net after all fees and sell cushion**, so it may need a higher bid. Loss to worthless = $20, not hypothetical -25% stop.
4. Candidate 0.19 delta rejected; 0.25 delta eligible only if quotes, spreads, balance, funds and strategy pass; 0.31 delta rejected; 0.25 delta with $0.40 ask rejected due full-premium + cushion >$40.
5. CPI 08:30 release not verified => no news-directed trade; at 09:55 ET on 2026-11-10 no order even if bullish ORB; at 10:02 on event day without separately validated event-day model => paper-only.
6. Release actual 0.4 vs forecast 0.3 cannot be used by bar timestamp before 08:30; headline/cpi mix labels do not automatically cause buy CALL/PUT; actual-and-forecast provenance recorded.
7. Three-second-old quote vs four-second-old quote; locked/crossed NBBO; no-bid option; cancellation race; paper fills vs live actual fills; derivative delta drift; no-lookahead tests with synthetic future-data perturbation.
8. On sell-to-close timeout, broker still owns contract => never submit a duplicate blindly; retry via documented safe cancel/replace or human escalation. Market close reconciliation detects any unexpected ETF shares/exercise.
9. Live requested today but 1000-session backtest or 60-day paper incomplete => **no live order** and reason documented. Neither user enthusiasm nor a profitable single paper day bypasses gates.
10. Profit +$6 followed by reversal: exit when bid-net gives back as configured (if target wasn't reached earlier). Never mark a profit from theoretical mid or from a quote touch at which the broker did not fill.
11. More than $40 requested **worst-case** exposure from simultaneous/pending order intents: reject atomically, despite racing threads or independent A/B/C strategy requests.
12. Cash settled after T+1 trading day only as broker-reported; a second trade same session can be blocked due to unsettled proceeds even though daily risk budget remains.

**Implementation sequence for Claude Code:** repo audit -> diff and schema tests -> risk ledger safety tests -> news/cpi timestamps -> options delta/affordability selector -> no-lookahead A/B/C signal engines -> historical quote and macro-vintage acquisition -> backtest/walk-forward/paper -> broker MCP adapter and order-reconciliation drill -> build complete daily scheduler/reports -> explicit live readiness report -> only if every gate already passed, separate operator-controlled LIVE release. Print `LIVE_BLOCKED` reasons rather than placing unverified orders.

## 19. Daily reports required
**Morning `daily-brief.md` fields:** date/time ET; account verified/type/option permissions/settled buying power; `LIVE_REQUESTED` vs `LIVE_APPROVED`; frozen equity; `DAILY_LOSS_ALLOWANCE=$40`, cumulative gross buys, pending entry reservations, fees contingency and available full-premium debit; macro calendar with official URLs, release/consensus/actual timestamps, classification and stale status; SPY/QQQ premarket price regime; validated A/B/C family, version/hash, news-day gate, option quote feed status, opening-range plan, first entry permitted time, no-trade reasons.

**After-each-trade:** symbol, call/put, expiry, strike, absolute delta (signal/entry), market/news regime, precise signal, broker order IDs/reviews, actual bid/ask/ask-buy and bid-sell fills, projected vs realized slippage, entry debit, exit proceeds, fees, net $, net % of morning account, maximum favorable/adverse excursion using executable bids, reason for full profit capture or loss, account gross-loss allowance remaining, unsettled cash.

**After-close `daily-report.md`:** `BROKER_CONFIRMED_FLAT=YES/NO`; all trade fills/partial orders, exercise warnings and unexpected shares, settlement dates, morning-to-close account change excluding deposits, net and gross loss budget, cum drawdown, all NO_TRADE and rejected signals by strategy, options affordability funnel, significant news timeline and actual market response, performance breakdown and sample size for A/B/C *without retroactive retuning*, proposed next-session changes marked `RESEARCH_ONLY`, next-day high-impact releases, alert and human followup list.

**Example safe status:**
```text
2026-11-10 08:45 ET | ACCOUNT_START=$200.00
NEWS: OCT-2026_CPI | BLS 08:30 | ACTUAL=SOURCE_VERIFICATION_PENDING
ENTRY_START=10:00 ET (CPI-day rule)
DELTA_TARGET=0.20..0.30 | ONE_CONTRACT_AT_A_TIME
DAILY_LOSS_MAX=$40 | FULL_PREMIUM_BUDGET=$35 LESS ACTUAL FEES/LOSSES
LIVE=REQUESTED_NOT_READY | ACTION=MONITOR_AND_PAPER
REASON=NO_VERIFIED_EVENT_DAY_EDGE / BROKER_LIVE_GATES_NOT_PROVED
```

## 20. Official primary references and transparency
- BLS November 2026 economic release calendar: https://www.bls.gov/schedule/2026/11_sched_list.htm
- BLS CPI releases: https://www.bls.gov/cpi/news.htm
- Robinhood external agent/Claude Code connection: https://robinhood.com/us/en/support/articles/agentic-trading-overview/
- Robinhood agent options and trade approval: https://robinhood.com/us/en/support/articles/trading-with-your-agent/
- Robinhood options trading hours/at-risk expiration closeout: https://robinhood.com/us/en/support/articles/options-trading-hours/
- Robinhood options exercise/expiration: https://robinhood.com/us/en/support/articles/expiration-exercise-and-assignment/
- Robinhood settlement and buying power: https://robinhood.com/us/en/support/articles/settlement-and-buying-power/
- Historical OPRA options data vendor example (verify coverage/licensing): https://databento.com/datasets/OPRA.PILLAR

**Non-claims:** This document is a complete design/update, **not a deployed service or brokerage connection**. Neither the 20–30 delta band nor news interpretation nor early profit taking is proven profitable. The research/paper gate may conclude that $200 cannot sustainably trade the required options; this is an acceptable result. Do not state that a $40 stop is a guarantee or that trading was turned on today without a broker-confirmed live event log.
