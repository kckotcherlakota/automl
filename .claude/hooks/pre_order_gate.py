#!/usr/bin/env python3
"""PreToolUse risk gate for Robinhood MCP order tools.

Deterministic, fail-closed. Reads the hook payload on stdin and DENIES the
tool call unless every condition holds:

  * the tool is an allowed order tool (equity / crypto); options, advanced
    orders and exercise are always denied (options disabled in this version)
  * state/mode.json says LIVE, is unexpired, and is bound to THIS session
  * the kill switch is off (sells/exits are still allowed when it is on)
  * the account is the allow-listed MCP account
  * a single-use authorization record written by `scripts/agent.py
    authorize-order` matches symbol, side, type, quantity and limit price,
    and has not expired; it is consumed atomically on use.

Any parse error or missing file => deny.
"""
from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

ROOT = Path(os.environ.get("TRADING_AGENT_ROOT", Path(__file__).resolve().parents[2]))
STATE = ROOT / "state"

ORDER_TOOLS = {"place_equity_order", "place_crypto_order"}
ALWAYS_DENY = {"place_option_order", "place_advanced_order", "exercise_option"}


def decide(decision: str, reason: str) -> None:
    # On a match we do NOT force-allow: the call continues through Claude Code's
    # normal permission flow and Robinhood's own customer-approval step.
    if decision == "deny":
        print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse",
                                                 "permissionDecision": "deny",
                                                 "permissionDecisionReason": reason}}))
    log({"decision": decision, "reason": reason})
    sys.exit(0)


_payload: dict = {}


def log(extra: dict) -> None:
    try:
        STATE.mkdir(parents=True, exist_ok=True)
        rec = {"at": int(time.time()), "tool": _payload.get("tool_name"), **extra,
               "input": _payload.get("tool_input")}
        with open(STATE / "order_gate_audit.jsonl", "a") as f:
            f.write(json.dumps(rec) + "\n")
    except Exception:
        pass


def norm_symbol(s: str) -> str:
    s = (s or "").upper().replace("-", "")
    return s[:-3] if s.endswith("USD") and len(s) > 3 else s


def load(p: Path) -> dict:
    with open(p) as f:
        return json.load(f)


def main() -> None:
    global _payload
    try:
        _payload = json.load(sys.stdin)
    except Exception:
        decide("deny", "order gate: unreadable hook payload")
    tool = _payload.get("tool_name", "")
    short = tool.split("__")[-1]
    args = _payload.get("tool_input") or {}
    if short in ALWAYS_DENY:
        decide("deny", f"order gate: {short} is disabled (options/advanced orders not permitted)")
    if short not in ORDER_TOOLS:
        decide("deny", f"order gate: unrecognised order tool {short}")

    try:
        trading = load(ROOT / "config" / "trading.json")
        mode = load(STATE / "mode.json") if (STATE / "mode.json").exists() else {"mode": "RESEARCH"}
    except Exception as e:  # noqa: BLE001
        decide("deny", f"order gate: cannot read config/state ({e})")

    now = time.time()
    acct = trading["live_account"]
    given = str(args.get("account_number") or args.get("rhs_account_number") or "")
    if given not in {str(acct["account_number"]), str(acct["rhs_account_number"])}:
        decide("deny", "order gate: account is not the allow-listed MCP account")

    side = str(args.get("side", "")).lower()
    kill = STATE / "kill_switch.json"
    kill_on = kill.exists() and bool(load(kill).get("engaged"))
    live_here = (mode.get("mode") == "LIVE" and now < float(mode.get("expires_at", 0))
                 and mode.get("session_id") == _payload.get("session_id"))
    if side == "sell":
        # Exits only reduce long exposure (no shorting in these accounts); they stay
        # available in a LIVE session and during a kill-switch emergency.
        if live_here or kill_on:
            decide("allow", "order gate: exit/protective sell permitted")
        decide("deny", "order gate: sells require a LIVE session or an engaged kill switch")
    if side != "buy":
        decide("deny", f"order gate: unsupported side {side!r}")
    if kill_on:
        decide("deny", "order gate: kill switch engaged (only exits allowed)")
    if mode.get("mode") != "LIVE":
        decide("deny", f"order gate: operating mode is {mode.get('mode')}, not LIVE")
    if now >= float(mode.get("expires_at", 0)):
        decide("deny", "order gate: LIVE activation expired; re-run readiness + set-mode")
    if mode.get("session_id") != _payload.get("session_id"):
        decide("deny", "order gate: LIVE activation belongs to a different Claude Code session")
    if args.get("dollar_amount"):
        decide("deny", "order gate: dollar_amount orders not allowed; use an exact quantity")

    auth_dir = STATE / "authorizations"
    sym = norm_symbol(str(args.get("symbol", "")))
    try:
        qty = float(args.get("quantity"))
    except Exception:
        decide("deny", "order gate: missing/invalid quantity")
    for p in sorted(auth_dir.glob("*.json")) if auth_dir.exists() else []:
        try:
            a = load(p)
        except Exception:
            continue
        if a.get("tool") != short or norm_symbol(a.get("symbol", "")) != sym or a.get("side") != side:
            continue
        if now >= float(a.get("expires_at", 0)):
            continue
        if a.get("session_id") != _payload.get("session_id"):
            continue
        if str(args.get("type", "")) != a.get("type"):
            continue
        if qty <= 0 or qty > float(a["max_quantity"]) + 1e-12:
            continue
        if a.get("type") in ("limit", "stop_limit"):
            try:
                lp = float(args.get("limit_price"))
            except Exception:
                continue
            if lp > float(a["max_limit_price"]) + 1e-9:
                continue
        used = p.with_suffix(".used")
        try:
            os.rename(p, used)  # atomic single-use consumption
        except OSError:
            continue
        decide("allow", f"order gate: matched authorization {p.stem}")
    decide("deny", "order gate: no matching, unexpired, single-use risk authorization")


if __name__ == "__main__":
    main()
