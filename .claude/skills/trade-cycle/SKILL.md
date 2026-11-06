---
name: trade-cycle
description: Run one complete deterministic evaluation cycle - connection, reconciliation, risk, scan, signals, decision, (paper) execution, journal, report. Use for "run one paper trading cycle".
---

# Trade Cycle

1. Mode check: RESEARCH => scan only (`/market-scan`). PAPER => `python3 scripts/agent.py paper-cycle` (crypto watchlist, public data, simulated fills at the ask with costs, durable lock prevents overlapping cycles). LIVE => follow `/crypto-trade` or `/stock-trade` step by step; never skip a gate.
2. Report: actions taken, positions, equity, risk mode, and why each strategy did or did not qualify.
3. Never place a real order from a paper cycle.
