# Backtest results — 2026-11-06 (fixed, untuned parameters; real cost model)

Data: Coinbase public candles (BTC/ETH/SOL: 2y hourly, 5y daily; BTC/ETH 180d 15m) and Yahoo daily bars
(SPY, QQQ, AAPL, NVDA, AMD, MSFT, TSLA, AMZN; 5y). Crypto cost = 0.95%/side Robinhood fee (measured).

Verdict: **no strategy passes acceptance on any symbol/timeframe.** Nothing is cleared for LIVE.
- Crypto 15m/1h: zero trades — the 1.9% round-trip fee exceeds 25% of every stop, so the cost filter rejects all signals.
  At zero fees, 15m strategies still lost (PF 0.74–0.92).
- Crypto daily: 2–10 trades per coin in 5 years; BTC/ETH negative, SOL positive (+$123 on $10k) but far too few trades.
- Stocks daily: strategy B (trend pullback) is positive on 5 of 8 symbols but pooled it is only 58 trades in 5 years,
  out-of-sample results are mixed-to-negative, and returns are a fraction of a percent per year at 0.25% risk.
Per-symbol tables: `backtest_<symbol>_<tf>_<date>.summary.json`.
