#!/usr/bin/env python3
"""Orchestration CLI for the Claude Code trading agent.

Claude Code is the agent; this script is glue that Claude (and scheduled
routines) call. It fetches public crypto data, feeds the C++ tools, keeps the
paper account, and manages mode / kill switch / authorizations.

It NEVER places broker orders. Live orders are placed only by Claude through
Robinhood MCP tools, and only after `authorize-order` wrote a single-use
record that the PreToolUse hook (.claude/hooks/pre_order_gate.py) checks.

Subcommands: fetch-crypto, ingest-mcp-bars, backtest, scan, paper-cycle,
risk-check, positions, report, kill-switch, set-mode, readiness,
authorize-order, recovery.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(os.environ.get("TRADING_AGENT_ROOT", Path(__file__).resolve().parents[1]))
BUILD = ROOT / "build"
STATE = ROOT / "state"
DATA = ROOT / "data"
REPORTS = ROOT / "reports"
DB = STATE / "journal.db"
ET = ZoneInfo("America/New_York")
UA = "claude-trading-agent/1.0"
TF_SEC = {"1m": 60, "5m": 300, "15m": 900, "1h": 3600, "1d": 86400}


# ----------------------------------------------------------------- helpers
def cfg(name: str) -> dict:
    with open(ROOT / "config" / f"{name}.json") as f:
        return json.load(f)


def read_json(p: Path, default=None):
    try:
        with open(p) as f:
            return json.load(f)
    except FileNotFoundError:
        return default


def write_json(p: Path, obj) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(p.suffix + ".tmp")
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=2)
    os.replace(tmp, p)


def tool(name: str, payload: dict) -> tuple[int, dict]:
    exe = BUILD / name
    if not exe.exists():
        sys.exit(f"missing binary {exe}; run scripts/build.sh")
    r = subprocess.run([str(exe), "--input", "-"], input=json.dumps(payload), capture_output=True, text=True)
    try:
        out = json.loads(r.stdout)
    except json.JSONDecodeError:
        out = {"error": r.stdout + r.stderr}
    return r.returncode, out


def journal(action: str, **kw) -> dict:
    rc, out = tool("trade_journal", {"db": str(DB), "action": action, "now": int(time.time()), **kw})
    if rc != 0:
        raise RuntimeError(out.get("error", out))
    return out


def mode() -> dict:
    m = read_json(STATE / "mode.json", {"mode": cfg("trading")["default_mode"]})
    if m.get("mode") == "LIVE" and time.time() >= float(m.get("expires_at", 0)):
        m = {"mode": "RESEARCH", "note": "LIVE activation expired"}
        write_json(STATE / "mode.json", m)
    return m


def kill_engaged() -> bool:
    return bool(read_json(STATE / "kill_switch.json", {}).get("engaged"))


class CycleLock:
    """Durable non-reentrant lock so overlapping cycles never run concurrently."""

    def __init__(self, name: str, stale_sec: int = 900):
        self.p = STATE / f"{name}.lock"
        self.stale = stale_sec

    def __enter__(self):
        STATE.mkdir(parents=True, exist_ok=True)
        try:
            fd = os.open(self.p, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            age = time.time() - self.p.stat().st_mtime
            if age < self.stale:
                print(json.dumps({"skipped": True, "reason": f"another cycle holds {self.p.name} ({age:.0f}s old)"}))
                sys.exit(0)
            self.p.unlink(missing_ok=True)
            fd = os.open(self.p, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        os.write(fd, str(os.getpid()).encode())
        os.close(fd)
        return self

    def __exit__(self, *a):
        self.p.unlink(missing_ok=True)


# ------------------------------------------------------------ market data
def http_json(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return json.loads(r.read())
        except Exception as e:  # noqa: BLE001
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)
            last = e  # noqa: F841


def coinbase_candles(product: str, tf: str, start: int, end: int) -> list[dict]:
    gran = TF_SEC[tf]
    out: dict[int, dict] = {}
    t = start
    while t < end:
        chunk_end = min(end, t + gran * 300)
        q = urllib.parse.urlencode({"granularity": gran,
                                    "start": dt.datetime.fromtimestamp(t, dt.timezone.utc).isoformat(),
                                    "end": dt.datetime.fromtimestamp(chunk_end, dt.timezone.utc).isoformat()})
        rows = http_json(f"https://api.exchange.coinbase.com/products/{product}/candles?{q}")
        for ts, lo, hi, op, cl, vol in rows:
            out[int(ts)] = {"t": int(ts), "o": op, "h": hi, "l": lo, "c": cl, "v": vol}
        t = chunk_end
        time.sleep(0.25)
    return [out[k] for k in sorted(out)]


def coinbase_quote(product: str) -> dict:
    j = http_json(f"https://api.exchange.coinbase.com/products/{product}/book?level=1")
    bid, ask = j["bids"][0], j["asks"][0]
    return {"bid": float(bid[0]), "bid_size": float(bid[1]), "ask": float(ask[0]), "ask_size": float(ask[1]),
            "ts": int(time.time()), "source": "coinbase_public_l1"}


def recent_crypto(product: str, tf: str = "15m", bars: int = 600) -> list[dict]:
    now = int(time.time())
    return coinbase_candles(product, tf, now - bars * TF_SEC[tf], now)


# --------------------------------------------------------------- commands
def cmd_fetch_crypto(a):
    end = int(time.time())
    for sym in a.symbols:
        bars = coinbase_candles(sym, a.tf, end - a.days * 86400, end)
        p = DATA / "crypto" / f"{sym}_{a.tf}.json"
        write_json(p, {"symbol": sym, "asset_class": "crypto", "timeframe": a.tf, "source": "coinbase_public",
                       "fetched_at": end, "bars": bars})
        print(json.dumps({"symbol": sym, "bars": len(bars), "file": str(p.relative_to(ROOT))}))


def cmd_ingest_mcp_bars(a):
    """Convert a saved Robinhood get_equity_historicals response to bars format."""
    raw = read_json(Path(a.file))
    res = raw.get("data", raw).get("results", [])
    for r in res:
        bars = []
        for b in r["bars"]:
            if b.get("interpolated"):
                continue
            t = int(dt.datetime.fromisoformat(b["begins_at"].replace("Z", "+00:00")).timestamp())
            bars.append({"t": t, "o": float(b["open_price"]), "h": float(b["high_price"]),
                         "l": float(b["low_price"]), "c": float(b["close_price"]), "v": float(b["volume"])})
        tf = {"5minute": "5m", "minute": "1m", "15minute": "15m", "hour": "1h", "day": "1d"}.get(r.get("interval"), a.tf)
        p = DATA / "stocks" / f"{r['symbol']}_{tf}.json"
        old = read_json(p, {"bars": []})["bars"]
        merged = {x["t"]: x for x in old + bars}
        write_json(p, {"symbol": r["symbol"], "asset_class": "stock", "timeframe": tf, "source": "robinhood_mcp",
                       "bars": [merged[k] for k in sorted(merged)]})
        print(json.dumps({"symbol": r["symbol"], "bars": len(merged), "file": str(p.relative_to(ROOT))}))


def cmd_backtest(a):
    d = read_json(Path(a.file))
    ac = d.get("asset_class", a.asset)
    tf = d.get("timeframe", a.tf)
    t = cfg("trading")
    costs = t["costs"][ac]
    strat = cfg("strategies")
    conf = {"initial_equity": a.equity, "risk_per_trade": cfg("risk")["risk_per_trade"],
            "max_position_notional": cfg("risk")["max_position_notional"], **{k: costs[k] for k in ("fee_bps", "slippage_bps", "spread_bps")},
            "max_hold_bars": t["crypto_max_hold_bars"], "include_research": a.include_research,
            "params": strat["params"]}
    if a.strategies:
        conf["strategies"] = a.strategies
    rc, out = tool("backtester", {"symbol": d.get("symbol", ""), "asset_class": ac, "timeframe": tf,
                                  "config": conf, "folds": a.folds, "bars": d["bars"]})
    if rc != 0:
        sys.exit(json.dumps(out))
    day = dt.date.today().isoformat()
    sym = d.get("symbol", "x")
    p = REPORTS / f"backtest_{sym}_{tf}_{day}.json"
    write_json(p, out)
    rows = []
    for name, s in out.items():
        if name.startswith("_"):
            continue
        f, te = s["full"], s["test_out_of_sample"]
        rows.append({"strategy": name, "trades": f["trades"], "net_pnl": round(f["net_pnl"], 2),
                     "win_rate": round(f["win_rate"], 3), "profit_factor": f["profit_factor"],
                     "max_dd": round(f["max_drawdown"], 4), "oos_trades": te["trades"],
                     "oos_expectancy": round(te["expectancy"], 4), "accepted": s["acceptance"]["accepted"],
                     "failures": s["acceptance"]["failures"]})
    summary = {"symbol": sym, "timeframe": tf, "bars": out["_meta"]["bars"], "report": str(p.relative_to(ROOT)), "strategies": rows}
    write_json(REPORTS / f"backtest_{sym}_{tf}_{day}.summary.json", summary)
    print(json.dumps(summary, indent=2))


def paper_state() -> dict:
    s = read_json(STATE / "paper_account.json")
    if s is None:
        eq = cfg("trading")["paper_starting_equity"]
        s = {"cash": eq, "equity": eq, "high_water_mark": eq, "start_day_equity": eq, "start_week_equity": eq,
             "start_month_equity": eq, "day": "", "week": "", "month": "", "consecutive_losses": 0,
             "last_loss_time": 0, "trades_today": 0, "positions": [], "persisted_mode": "NORMAL",
             "created_at": int(time.time())}
    return s


def roll_periods(s: dict) -> None:
    now = dt.datetime.now(ET)
    day, week, month = now.date().isoformat(), f"{now.isocalendar()[0]}-W{now.isocalendar()[1]}", now.strftime("%Y-%m")
    if s["day"] != day:
        s["day"], s["start_day_equity"], s["trades_today"] = day, s["equity"], 0
    if s["week"] != week:
        s["week"], s["start_week_equity"] = week, s["equity"]
    if s["month"] != month:
        s["month"], s["start_month_equity"] = month, s["equity"]


def account_payload(s: dict, op_mode: str, unknown: int = 0) -> dict:
    return {"equity": s["equity"], "buying_power": s["cash"], "start_day_equity": s["start_day_equity"],
            "start_week_equity": s["start_week_equity"], "start_month_equity": s["start_month_equity"],
            "high_water_mark": s["high_water_mark"], "consecutive_losses": s["consecutive_losses"],
            "trades_today": s["trades_today"], "last_loss_time": s["last_loss_time"], "now": int(time.time()),
            "kill_switch": kill_engaged(), "persisted_mode": s.get("persisted_mode", "NORMAL"),
            "operating_mode": op_mode, "unknown_orders": unknown,
            "positions": [{"symbol": p["symbol"], "asset_class": p["asset_class"], "qty": p["qty"],
                           "entry": p["entry"], "stop": p["stop"], "mark": p.get("mark", p["entry"])}
                          for p in s["positions"]]}


def manage_paper_position(p: dict, bars: list[dict], tf: str, side_cost: float, max_hold: int):
    """Walk completed bars after the last processed bar; return (exit_px, reason) or None."""
    now = int(time.time())
    for b in bars:
        if b["t"] <= p["last_bar_t"] or b["t"] + TF_SEC[tf] > now:
            continue
        p["last_bar_t"] = b["t"]
        p["bars_held"] = p.get("bars_held", 0) + 1
        p["mfe_px"] = max(p.get("mfe_px", p["entry"]), b["h"])
        p["mae_px"] = min(p.get("mae_px", p["entry"]), b["l"])
        p["mark"] = b["c"]
        if b["o"] <= p["stop"]:
            return b["o"] * (1 - side_cost), "stop_gap"
        if b["l"] <= p["stop"]:
            return p["stop"] * (1 - side_cost), "stop"
        if b["h"] >= p["target"]:
            return max(p["target"], b["o"]) * (1 - side_cost), "target"
        if p["bars_held"] >= max_hold:
            return b["c"] * (1 - side_cost), "time_exit"
    return None


def cmd_paper_cycle(a):
    m = mode()
    if m["mode"] == "LIVE":
        sys.exit("paper-cycle refuses to run while LIVE mode is active")
    t, wl, strat, risk = cfg("trading"), cfg("watchlist"), cfg("strategies"), cfg("risk")
    tf = wl["crypto"]["timeframe"]
    costs = t["costs"]["crypto"]
    side_cost = (costs["slippage_bps"] + costs["spread_bps"] / 2 + costs["fee_bps"]) / 1e4
    log = {"at": int(time.time()), "mode": "PAPER", "symbols": {}}
    with CycleLock("paper_cycle"):
        s = paper_state()
        roll_periods(s)
        unknown = journal("unknown_count", mode="PAPER")["unknown_orders"]
        for sym in wl["crypto"]["symbols"]:
            entry = {"actions": []}
            try:
                bars = recent_crypto(sym, tf, 600)
            except Exception as e:  # noqa: BLE001
                entry["error"] = f"data fetch failed: {e}"
                log["symbols"][sym] = entry
                continue
            # 1) manage open position
            pos = next((p for p in s["positions"] if p["symbol"] == sym), None)
            if pos:
                ex = manage_paper_position(pos, bars, tf, side_cost, t["crypto_max_hold_bars"])
                if ex:
                    px, why = ex
                    pnl = (px - pos["entry"]) * pos["qty"]
                    s["cash"] += pos["qty"] * px
                    s["positions"].remove(pos)
                    r_mult = pnl / ((pos["entry"] - pos["stop"]) * pos["qty"])
                    journal("close_trade", trade={"mode": "PAPER", "symbol": sym, "strategy": pos["strategy"],
                                                  "qty": pos["qty"], "entry": pos["entry"], "exit": px, "pnl": pnl,
                                                  "r_multiple": r_mult, "opened_at": pos["opened_at"],
                                                  "exit_reason": why, "entry_order_id": pos["order_id"]})
                    if pnl < 0:
                        s["consecutive_losses"] += 1
                        s["last_loss_time"] = int(time.time())
                    else:
                        s["consecutive_losses"] = 0
                    entry["actions"].append({"exit": why, "price": px, "pnl": pnl, "r": r_mult})
            # 2) evaluate new entry
            if not any(p["symbol"] == sym for p in s["positions"]):
                rc, sig = tool("signal_engine", {"symbol": sym, "asset_class": "crypto", "timeframe": tf,
                                                 "now": int(time.time()), "params": strat["params"],
                                                 "strategies": [k for k, v in strat["enabled"].items() if v == "paper"],
                                                 "bars": bars})
                entry["signal"] = {"summary": sig.get("summary"), "regime": sig.get("regime", {}).get("label"),
                                   "rejected": {r["strategy"]: r["failed"] for r in sig.get("rejected", [])}}
                if rc == 0 and sig.get("qualified"):
                    best = sig["qualified"][0]
                    q = coinbase_quote(sym)
                    erc, ex = tool("order_optimizer", {"config": t["execution"]["crypto"], "quote": q,
                                                      "signal_price": best["entry"], "tick_size": t["tick_size"][sym],
                                                      "now": int(time.time())})
                    entry["execution"] = ex
                    if erc == 0:
                        _mark_positions(s, sym, bars)
                        rrc, dec = tool("risk_engine", {"config": risk, "account": account_payload(s, "PAPER", unknown),
                                                        "proposal": {"symbol": sym, "asset_class": "crypto",
                                                                     "strategy": best["strategy"], "entry": ex["limit_price"],
                                                                     "stop": best["stop"],
                                                                     "slippage_per_unit": ex["est_fill_slippage"],
                                                                     "qty_increment": t["qty_increment"][sym],
                                                                     "research_only": best["research_only"]}})
                        entry["risk"] = dec
                        if rrc == 0:
                            entry["actions"].append(_paper_fill(s, sym, best, ex, dec, q, sig["signal_bar_t"]))
            log["symbols"][sym] = entry
        _mark_equity(s)
        s["persisted_mode"] = tool("risk_engine", {"config": risk, "account": account_payload(s, "PAPER", unknown),
                                                   "action": "mode"})[1].get("mode", "LOCKED")
        write_json(STATE / "paper_account.json", s)
    log["equity"] = s["equity"]
    log["risk_mode"] = s["persisted_mode"]
    STATE.mkdir(exist_ok=True)
    with open(STATE / "paper_cycles.jsonl", "a") as f:
        f.write(json.dumps(log) + "\n")
    print(json.dumps(log, indent=2))


def _mark_positions(s: dict, sym: str, bars: list[dict]) -> None:
    for p in s["positions"]:
        if p["symbol"] == sym and bars:
            p["mark"] = bars[-1]["c"]


def _mark_equity(s: dict) -> None:
    s["equity"] = s["cash"] + sum(p["qty"] * p.get("mark", p["entry"]) for p in s["positions"])
    s["high_water_mark"] = max(s["high_water_mark"], s["equity"])


def _paper_fill(s, sym, best, ex, dec, q, signal_t) -> dict:
    oid = f"paper-{uuid.uuid4()}"
    qty = dec["quantity"]
    journal("propose", order={"client_order_id": oid, "mode": "PAPER", "intent_key": f"{sym}:{best['strategy']}:{signal_t}",
                              "symbol": sym, "asset_class": "crypto", "qty": qty, "limit_price": ex["limit_price"],
                              "stop": best["stop"], "target": best["target"], "strategy": best["strategy"]})
    for st in ("REVIEWED", "APPROVED_BY_RISK", "SUBMITTED"):
        journal("transition", client_order_id=oid, to=st, detail={"paper": True})
    # Conservative paper fill: pay the ask (+ modelled slippage), not the limit.
    px = q["ask"] * (1 + cfg("trading")["costs"]["crypto"]["slippage_bps"] / 1e4)
    journal("fill", client_order_id=oid, qty=qty, price=px, fill_id=f"{oid}-1")
    journal("transition", client_order_id=oid, to="FILLED", detail={"paper_fill_price": px})
    s["cash"] -= qty * px
    s["trades_today"] += 1
    s["positions"].append({"symbol": sym, "asset_class": "crypto", "qty": qty, "entry": px, "stop": best["stop"],
                           "target": best["target"], "strategy": best["strategy"], "order_id": oid,
                           "opened_at": int(time.time()), "last_bar_t": signal_t, "mark": px})
    return {"entry": best["strategy"], "qty": qty, "fill": px, "stop": best["stop"], "target": best["target"],
            "risk_dollars": dec["risk_dollars"]}


def cmd_scan(a):
    wl, strat, t = cfg("watchlist"), cfg("strategies"), cfg("trading")
    out = {"at": int(time.time()), "mode": mode()["mode"], "kill_switch": kill_engaged(), "results": {}}
    enabled = [k for k, v in strat["enabled"].items() if v in ("paper", "live")]
    for sym in wl["crypto"]["symbols"]:
        try:
            bars = recent_crypto(sym, wl["crypto"]["timeframe"], 600)
        except Exception as e:  # noqa: BLE001
            out["results"][sym] = {"error": str(e)}
            continue
        out["results"][sym] = _scan_one(sym, "crypto", wl["crypto"]["timeframe"], bars, strat, enabled)
    for f in sorted((DATA / "stocks").glob("*_5m.json")) if (DATA / "stocks").exists() else []:
        d = read_json(f)
        out["results"][d["symbol"]] = _scan_one(d["symbol"], "stock", "5m", d["bars"], strat, enabled)
    out["any_qualified"] = any(r.get("qualified") for r in out["results"].values())
    write_json(REPORTS / f"market_scan_{dt.datetime.now(ET):%Y-%m-%d_%H%M}.json", out)
    print(json.dumps(out, indent=2))


def _scan_one(sym, ac, tf, bars, strat, enabled):
    now = int(time.time())
    _, an = tool("market_analyzer", {"symbol": sym, "asset_class": ac, "timeframe": tf, "now": now, "bars": bars})
    rc, sig = tool("signal_engine", {"symbol": sym, "asset_class": ac, "timeframe": tf, "now": now,
                                     "params": strat["params"], "strategies": enabled, "bars": bars})
    return {"close": an.get("close"), "regime": an.get("regime"), "data_age_sec": sig.get("data_age_sec"),
            "stale": sig.get("stale"), "indicators": an.get("indicators"),
            "qualified": sig.get("qualified", []),
            "rejected": {r["strategy"]: r["failed"] for r in sig.get("rejected", [])}}


def cmd_risk_check(a):
    risk = cfg("risk")
    if a.live_snapshot:
        acct = read_json(Path(a.live_snapshot))
        acct.setdefault("now", int(time.time()))
        acct.setdefault("kill_switch", kill_engaged())
        src = "live snapshot (from Robinhood MCP)"
    else:
        s = paper_state()
        roll_periods(s)
        acct = account_payload(s, "PAPER", journal("unknown_count", mode="PAPER")["unknown_orders"])
        src = "paper account"
    rc, me = tool("risk_engine", {"config": risk, "account": acct, "action": "mode"})
    exposure = sum(p["qty"] * p.get("mark", p.get("entry", 0)) for p in acct.get("positions", []))
    print(json.dumps({"source": src, "operating_mode": mode()["mode"], "kill_switch": kill_engaged(),
                      "equity": acct["equity"], "buying_power": acct["buying_power"],
                      "open_exposure": exposure, "exposure_pct": exposure / acct["equity"],
                      "daily_pnl": acct["equity"] - acct.get("start_day_equity", acct["equity"]),
                      "weekly_pnl": acct["equity"] - acct.get("start_week_equity", acct["equity"]),
                      "risk_state": me,
                      "new_entries_allowed": me.get("mode") in ("NORMAL", "CAUTION") and not kill_engaged()
                      and mode()["mode"] != "RESEARCH"}, indent=2))


def cmd_positions(a):
    s = paper_state()
    print(json.dumps({"paper_positions": s["positions"], "paper_open_orders": journal("open_orders", mode="PAPER")["open_orders"],
                      "live_open_orders_journal": journal("open_orders", mode="LIVE")["open_orders"],
                      "note": "Live broker positions must be fetched with Robinhood MCP get_crypto_positions / "
                              "get_equity_positions and are authoritative over this journal."}, indent=2))


def cmd_report(a):
    since = int(time.time()) - a.days * 86400
    rep = {"paper": journal("report", mode="PAPER", since=since), "live": journal("report", mode="LIVE", since=since),
           "paper_account": paper_state(), "mode": mode(), "kill_switch": kill_engaged()}
    cycles = []
    if (STATE / "paper_cycles.jsonl").exists():
        cycles = [json.loads(x) for x in open(STATE / "paper_cycles.jsonl")][-5:]
    rep["last_paper_cycles"] = [{"at": c["at"], "equity": c.get("equity"), "risk_mode": c.get("risk_mode")} for c in cycles]
    write_json(REPORTS / f"trade_report_{dt.date.today()}.json", rep)
    print(json.dumps(rep, indent=2))


def cmd_kill_switch(a):
    engaged = a.state == "on"
    write_json(STATE / "kill_switch.json", {"engaged": engaged, "at": int(time.time()), "reason": a.reason})
    if engaged:
        write_json(STATE / "mode.json", {"mode": "RESEARCH", "note": "kill switch engaged", "at": int(time.time())})
        for p in (STATE / "authorizations").glob("*.json") if (STATE / "authorizations").exists() else []:
            p.rename(p.with_suffix(".revoked"))
    try:
        journal("audit", kind="kill_switch", detail={"engaged": engaged, "reason": a.reason})
    except Exception:  # noqa: BLE001
        pass
    print(json.dumps({"kill_switch": engaged, "mode": mode()["mode"],
                      "next_steps": ["Cancel pending OPENING orders via Robinhood MCP cancel_* tools",
                                     "Reconcile positions with get_crypto_positions / get_equity_positions",
                                     "Exits (sells) remain allowed by the order gate"] if engaged else []}, indent=2))


def readiness_report() -> dict:
    t = cfg("trading")["readiness"]
    checks = []
    r = subprocess.run(["ctest", "--test-dir", str(BUILD)], capture_output=True, text=True)
    checks.append({"check": "unit tests pass", "ok": r.returncode == 0})
    accepted = []
    for f in REPORTS.glob("backtest_*.summary.json"):
        for row in read_json(f)["strategies"]:
            if row["accepted"]:
                accepted.append(f"{read_json(f)['symbol']}:{row['strategy']}")
    checks.append({"check": "at least one strategy passed out-of-sample acceptance", "ok": bool(accepted), "detail": accepted})
    s = read_json(STATE / "paper_account.json")
    days = (time.time() - s["created_at"]) / 86400 if s else 0
    checks.append({"check": f">= {t['min_paper_days']} days of paper observation", "ok": days >= t["min_paper_days"], "detail": round(days, 2)})
    n = journal("report", mode="PAPER", since=0)["count"] if DB.exists() else 0
    checks.append({"check": f">= {t['min_paper_trades']} paper trades", "ok": n >= t["min_paper_trades"], "detail": n})
    hook = read_json(ROOT / ".claude" / "settings.json", {})
    checks.append({"check": "PreToolUse order gate installed", "ok": "pre_order_gate.py" in json.dumps(hook)})
    checks.append({"check": "kill switch off", "ok": not kill_engaged()})
    return {"ready": all(c["ok"] for c in checks), "checks": checks}


def cmd_readiness(a):
    print(json.dumps(readiness_report(), indent=2))


def cmd_set_mode(a):
    if a.mode == "LIVE":
        rep = readiness_report()
        if not rep["ready"]:
            print(json.dumps({"mode": mode()["mode"], "refused": "readiness checks failed", **rep}, indent=2))
            sys.exit(3)
        if not a.confirm_live or not a.session_id or not a.operator:
            sys.exit("LIVE requires --confirm-live, --operator NAME and --session-id (the current Claude Code session)")
        hours = min(a.hours, cfg("trading")["live_activation_max_hours"])
        m = {"mode": "LIVE", "session_id": a.session_id, "activated_by": a.operator, "at": int(time.time()),
             "expires_at": int(time.time() + hours * 3600)}
    else:
        m = {"mode": a.mode, "at": int(time.time())}
    write_json(STATE / "mode.json", m)
    journal("audit", kind="set_mode", detail=m)
    print(json.dumps(m, indent=2))


def cmd_authorize_order(a):
    """Write a single-use authorization for one LIVE order after re-running risk + execution checks."""
    m = mode()
    if m["mode"] != "LIVE":
        sys.exit(f"refused: mode is {m['mode']} (LIVE required)")
    req = read_json(Path(a.request))
    t, risk = cfg("trading"), cfg("risk")
    ac = req["asset_class"]
    erc, ex = tool("order_optimizer", {"config": t["execution"][ac], "quote": req["quote"], "signal_price": req["signal_price"],
                                       "tick_size": req["tick_size"], "now": int(time.time())})
    if erc != 0:
        print(json.dumps({"authorized": False, "execution": ex}, indent=2)); sys.exit(3)
    acct = req["account_state"]; acct["operating_mode"] = "LIVE"; acct["kill_switch"] = kill_engaged()
    acct["now"] = int(time.time())
    acct["unknown_orders"] = journal("unknown_count", mode="LIVE")["unknown_orders"]
    rrc, dec = tool("risk_engine", {"config": risk, "account": acct,
                                    "proposal": {"symbol": req["symbol"], "asset_class": ac, "strategy": req["strategy"],
                                                 "entry": ex["limit_price"], "stop": req["stop"],
                                                 "slippage_per_unit": ex["est_fill_slippage"],
                                                 "qty_increment": req["qty_increment"], "research_only": req.get("research_only", False)}})
    if rrc != 0:
        print(json.dumps({"authorized": False, "risk": dec}, indent=2)); sys.exit(3)
    ref = str(uuid.uuid4())
    rec = {"ref_id": ref, "tool": "place_crypto_order" if ac == "crypto" else "place_equity_order",
           "session_id": m["session_id"], "symbol": req["symbol"], "side": "buy", "type": "limit",
           "max_quantity": dec["quantity"], "max_limit_price": ex["limit_price"],
           "stop": req["stop"], "expires_at": int(time.time()) + cfg("trading")["authorization_ttl_sec"],
           "risk_decision": dec, "execution": ex}
    write_json(STATE / "authorizations" / f"{ref}.json", rec)
    journal("propose", order={"client_order_id": ref, "mode": "LIVE", "intent_key": req["intent_key"], "symbol": req["symbol"],
                              "asset_class": ac, "qty": dec["quantity"], "limit_price": ex["limit_price"], "stop": req["stop"],
                              "target": req.get("target", 0), "strategy": req["strategy"]})
    for st in ("REVIEWED", "APPROVED_BY_RISK"):
        journal("transition", client_order_id=ref, to=st, detail={})
    print(json.dumps({"authorized": True, "ref_id": ref, "place_with": {
        "quantity": f"{dec['quantity']:.8f}".rstrip("0").rstrip("."), "limit_price": f"{ex['limit_price']:.2f}",
        "type": "limit", "side": "buy", "symbol": req["symbol"], "ref_id": ref},
        "expires_in_sec": cfg("trading")["authorization_ttl_sec"]}, indent=2))


def cmd_recovery(a):
    rep = journal("report", mode=a.mode, since=0)
    losers = [t for t in rep["trades"] if t["pnl"] < 0][-a.last:]
    by = {}
    for t in losers:
        b = by.setdefault(t["strategy"], {"losses": 0, "pnl": 0.0, "exit_reasons": {}})
        b["losses"] += 1; b["pnl"] += t["pnl"]
        b["exit_reasons"][t["exit_reason"]] = b["exit_reasons"].get(t["exit_reason"], 0) + 1
    s = paper_state()
    out = {"mode": a.mode, "losing_trades_analysed": len(losers), "by_strategy": by,
           "current_risk_mode": s.get("persisted_mode"),
           "policy": "Recovery never increases size. CAUTION halves-to-0.10% risk; RECOVERY blocks new entries; "
                     "LOCKED requires explicit operator unlock via drawdown_manager.",
           "questions": ["Which strategy lost money?", "Was the regime unsuitable?", "Were spreads wide?",
                         "Was execution delayed?", "Were positions correlated?", "Did costs remove the edge?",
                         "Did a stop fail?", "Has expectancy deteriorated vs backtest?"]}
    write_json(REPORTS / f"recovery_review_{dt.date.today()}.json", out)
    print(json.dumps(out, indent=2))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("fetch-crypto"); p.add_argument("--symbols", nargs="+", default=["BTC-USD", "ETH-USD"])
    p.add_argument("--tf", default="15m"); p.add_argument("--days", type=int, default=120); p.set_defaults(f=cmd_fetch_crypto)
    p = sp.add_parser("ingest-mcp-bars"); p.add_argument("file"); p.add_argument("--tf", default="5m"); p.set_defaults(f=cmd_ingest_mcp_bars)
    p = sp.add_parser("backtest"); p.add_argument("file"); p.add_argument("--asset", default="crypto"); p.add_argument("--tf", default="15m")
    p.add_argument("--equity", type=float, default=10000); p.add_argument("--folds", type=int, default=4)
    p.add_argument("--strategies", nargs="*"); p.add_argument("--include-research", action="store_true"); p.set_defaults(f=cmd_backtest)
    sp.add_parser("scan").set_defaults(f=cmd_scan)
    sp.add_parser("paper-cycle").set_defaults(f=cmd_paper_cycle)
    p = sp.add_parser("risk-check"); p.add_argument("--live-snapshot"); p.set_defaults(f=cmd_risk_check)
    sp.add_parser("positions").set_defaults(f=cmd_positions)
    p = sp.add_parser("report"); p.add_argument("--days", type=int, default=7); p.set_defaults(f=cmd_report)
    p = sp.add_parser("kill-switch"); p.add_argument("state", choices=["on", "off"]); p.add_argument("--reason", default="operator")
    p.set_defaults(f=cmd_kill_switch)
    p = sp.add_parser("set-mode"); p.add_argument("mode", choices=["RESEARCH", "PAPER", "LIVE"]); p.add_argument("--confirm-live", action="store_true")
    p.add_argument("--session-id"); p.add_argument("--operator"); p.add_argument("--hours", type=float, default=4); p.set_defaults(f=cmd_set_mode)
    sp.add_parser("readiness").set_defaults(f=cmd_readiness)
    p = sp.add_parser("authorize-order"); p.add_argument("request"); p.set_defaults(f=cmd_authorize_order)
    p = sp.add_parser("recovery"); p.add_argument("--mode", default="PAPER"); p.add_argument("--last", type=int, default=20); p.set_defaults(f=cmd_recovery)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
