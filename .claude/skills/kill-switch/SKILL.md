---
name: kill-switch
description: Immediately stop new trading - persist the lock, revoke authorizations, cancel pending opening orders, reconcile positions, keep only exits. Use for "stop new trading immediately".
---

# Kill Switch

1. `scripts/emergency-stop.sh "<reason>"` (sets kill switch, forces RESEARCH mode, revokes authorizations).
2. MCP: list open orders (`get_crypto_orders`, `get_equity_orders`) and cancel pending BUY/opening orders with `cancel_crypto_order` / `cancel_equity_order`. Do not cancel protective stop orders on open positions.
3. Reconcile positions and report any unresolved exposure.
4. Turning it off requires the operator: `python3 scripts/agent.py kill-switch off --reason ...`.
