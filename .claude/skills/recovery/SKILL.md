---
name: recovery
description: Analyse losing periods, apply the drawdown state machine, and produce a recovery review without ever increasing size or forcing trades. Use for "activate recovery mode" or "why did the last five trades lose money".
---

# Recovery

1. `python3 scripts/agent.py recovery --last 20`.
2. Run `build/drawdown_manager` with the account state to confirm the mode. Recovery never raises risk; CAUTION uses 0.10% risk, RECOVERY blocks entries, LOCKED needs an explicit operator unlock (`operator_unlock` with `by` and `reason`).
3. Compare live/paper results with the latest backtest for the same strategy (regime, spread/fees, slippage, correlation, failed stops).
4. Never martingale, average down, or trade to "win back" a loss.
