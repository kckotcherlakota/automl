# Master Prompt (operator brief, 2026-11-06) — condensed record

The operator's full brief, by section, as implemented in this repository.

1. **Role / architecture** — Claude Code *is* the application: no web app, dashboard, Java/Spring, or standalone
   bot. Use Robinhood Trading MCP, C++20 for computation, Python only where useful, skills, hooks, local state,
   scheduling where supported. Priority: capital preservation > order safety > risk-adjusted profit > consistency >
   drawdown control > trade quality > speed > opportunity discovery. No strategy guarantees profit.
2. **Robinhood MCP** — verify connection, the dedicated MCP account, permissions, buying power, positions, orders,
   restrictions, approval settings. Trade only the MCP account. Discover tool schemas; never invent them. No live
   trading during implementation, backtesting or verification.
3. **Engine flow** — data via MCP → C++ signals/diagnostics → Claude evaluates eligibility → C++ risk validation →
   broker preview → independent final risk gate → place only if live is explicitly authorized → monitor → record →
   managed exits. Claude never overrides a failed deterministic check; AI confidence is not a calibrated probability.
4. **C++ tools** — market_analyzer, signal_engine, risk_engine, order_optimizer, market_regime, backtester,
   drawdown_manager, trade_journal, benchmark; C++20/CMake/GoogleTest/Google Benchmark; structured I/O, input
   validation, meaningful exit codes, tests; incremental indicators; no claim that local speed = exchange speed.
5. **Universe** — stocks SPY, QQQ, AAPL, NVDA, AMD, MSFT, TSLA, AMZN (SPY/QQQ first for paper); crypto BTC/USD,
   ETH/USD (SOL only after independent testing). Long stock and spot crypto only; no margin, shorts, futures,
   leverage, averaging down, martingale, transfers. Options disabled.
6. **Stock strategies** — A Opening Range Breakout (5m, 9:30–9:45 range, VWAP, EMA9>EMA21, ATR14, RVOL≥1.5,
   spread, prefer retest, completed candle, structure stop with ATR validation, ≥2:1 net of costs); B Trend
   following (EMA20/50/200, RSI, ADX>20, VWAP, stabilized pullback, reversal); C Momentum breakout (RVOL, 20-bar
   breakout, HTF bullish, healthy volatility, no chasing); D VWAP mean reversion (research-only).
7. **Crypto strategies** — E BTC/ETH trend momentum (15m + 1h EMA20>50, RSI 50–70, 20-candle breakout, RVOL≥1.5,
   ATR/structure stop, ~2R); F trend pullback; G volatility breakout (research-only); H microstructure (research,
   execution filter only; no HFT assumptions over Robinhood MCP).
8. **Capital protection** — 0.25% risk/trade (max 0.50%), 10% per position, 30% stocks, 15% crypto, 40% total,
   ≥60% uncommitted, ≤3 positions, ≤6 trades/day, 1%/3%/5% daily/weekly/monthly loss triggers, 8% hard drawdown
   lock, pause after 3 losses, 30-min cooldown; correlation-aware; sizing = min(risk qty, exposure qty, buying
   power, remaining exposure), rounded down; every position has reason, entry, protective exit, invalidation, max
   loss, time exit; prefer broker-native protection and never claim protection without broker acceptance.
9. **Profit protection** — track MFE/MAE etc.; trailing/partials only if validated; never widen stops.
10. **Recovery** — NORMAL → CAUTION (2% dd or 3 losses: 0.10% risk, validated setups only) → RECOVERY (4%: no new
    live entries, diagnose) → LOCKED (8%, critical failure or hard loss limit: block, cancel opening orders,
    reconcile, operator unlock only). Never martingale, revenge trade, average down or size up to recover.
11. **Execution** — 20-step sequence; order state machine PROPOSED…UNKNOWN; UNKNOWN blocks entries; timeouts are not
    failures; no duplicate orders; PreToolUse hook gate with short-lived single-use authorizations; if hooks cannot
    reliably intercept every order tool, keep unattended live trading disabled.
12. **Order optimisation** — limit orders, spread/slippage/staleness checks, max order age, max retries, no
    endless repricing, respect broker rate limits.
13. **Backtesting** — real C++ engine, costs, slippage, sessions, no look-ahead, chronological train/val/test,
    walk-forward, sensitivity, bootstrap; full metric list; acceptance: positive OOS expectancy after costs,
    PF>1.20, enough trades, stable windows, acceptable drawdown. Never fabricate results.
14. **Skills** — /market-scan /stock-trade /crypto-trade /risk-check /positions /backtest /trade-report /recovery
    /kill-switch /trade-cycle; safety instructions persisted in CLAUDE.md.
15. **Scheduling** — investigate /loop and /schedule; don't assume background sessions inherit MCP auth; paper
    first; durable locks; skip overlapping cycles.
16. **Modes** — RESEARCH (default), PAPER, LIVE (never automatic; requires tests, positive OOS, ≥30 paper days,
    enough trades, verified permissions, working gates, reconciliation, emergency stop, explicit approval; expires;
    not carried into fresh sessions; never a live test order during setup).
17. **Persistence** — SQLite + JSON; unique constraints for orders; reconcile with Robinhood on start; broker is
    authoritative; no credentials in logs or git.
18. **Layout** — CLAUDE.md, README.md, CMakeLists.txt, config/, cpp/{include,src,tests,benchmarks}, scripts/,
    .claude/{skills,hooks,settings.json}, data/, state/, reports/, tests/.
19. **Safety tests** — stale data, invalid indicators/prices, insufficient funds, sizing, exposure, correlation,
    loss triggers, drawdown transitions, duplicates, partial fills, UNKNOWN states, connection loss, quote changes,
    interruption, failed protection, kill switch, paper/live separation, account mismatch — all with mocks.
20. **Operating rules** — the 20 rules copied into CLAUDE.md.
21. **Build plan** — phases 1–8 ending with a deployment-readiness report and real trading left disabled.
22. **Expected result** — natural-language control of a disciplined, testable workflow that knows when not to trade.
