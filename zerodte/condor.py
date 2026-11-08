"""High-win-rate premium-selling 0DTE study on SPY/QQQ (same data as engine.py).

Structures: iron condor (short call + put spreads) or a single put/call credit
spread. Strikes are chosen by Black-Scholes delta at entry; prices are
modelled as in options_model.py (flat IV -> no skew; see REPORT.md).

Exit rules: stop when the cost to close >= credit * (1 + stop_mult) (e.g.
stop_mult=2 -> lose 200% of credit), optional profit-take, else hold to the
16:00 close and settle at intrinsic. Stops are checked against each 5-min
bar's extreme (high for calls, low for puts); if a bar opens beyond the stop
we fill at the open.
"""
from __future__ import annotations

import itertools
import json
import sys
from dataclasses import dataclass, asdict, replace
from datetime import time
from multiprocessing import Pool
from pathlib import Path

import numpy as np
import pandas as pd

from options_model import MIN_PER_YEAR, bs_price, half_spread, pick_strike, load_iv_table

HERE = Path(__file__).parent


@dataclass
class CP:
    structure: str = "condor"        # condor | put_spread | call_spread
    short_delta: float = 0.20
    long_delta: float = 0.05
    entry: time = time(9, 45)        # entry at this clock time (close of prior bar)
    stop_mult: float = 2.0           # loss stop as multiple of credit; None = no stop
    take_profit: float | None = None # fraction of credit captured, e.g. 0.5
    risk_per_trade: float = 0.01     # fraction of account at risk (max loss)
    wing_width: float | None = None  # fixed $ wing instead of long_delta (e.g. 1.0)
    fixed_contracts: int | None = None
    fee_per_contract_side: float = 0.65


def _mins_to_close(ts_end) -> float:
    return 960 - (ts_end.hour * 60 + ts_end.minute)


def _legs(S, T, iv, p: CP):
    legs = []  # (K, is_call, qty)  qty -1 short, +1 long
    if p.structure in ("condor", "call_spread"):
        ks = pick_strike(S, T, iv, True, p.short_delta)
        kl = ks + p.wing_width if p.wing_width else max(pick_strike(S, T, iv, True, p.long_delta), ks + 1)
        legs += [(ks, True, -1), (kl, True, +1)]
    if p.structure in ("condor", "put_spread"):
        ks = pick_strike(S, T, iv, False, p.short_delta)
        kl = ks - p.wing_width if p.wing_width else min(pick_strike(S, T, iv, False, p.long_delta), ks - 1)
        legs += [(ks, False, -1), (kl, False, +1)]
    return legs


def _close_cost(legs, S, T, iv, sym):
    """Debit to buy back the structure (pay ask on shorts, hit bid on longs)."""
    c = 0.0
    for K, is_call, q in legs:
        m = float(bs_price(S, K, T, iv, is_call))
        hs = half_spread(m, sym) if m > 0.005 else 0.0
        c += (m + hs) if q < 0 else -max(0.0, m - hs)
    return c


def _intrinsic(legs, S):
    v = 0.0
    for K, is_call, q in legs:
        v += -q * (max(0.0, S - K) if is_call else max(0.0, K - S))
    return v  # cost to close at expiry


def run_day(day: pd.DataFrame, sym: str, iv: float, p: CP, equity: float):
    entry_bar = day[(day["ts"] + pd.Timedelta(minutes=5)).dt.time == p.entry]
    if entry_bar.empty:
        return None
    i = entry_bar.index[0]
    r = day.loc[i]
    S0 = r["close"]
    T0 = _mins_to_close(r["ts"] + pd.Timedelta(minutes=5)) / MIN_PER_YEAR
    legs = _legs(S0, T0, iv, p)
    # opening: shorts at bid, longs at ask
    credit = 0.0
    for K, is_call, q in legs:
        m = float(bs_price(S0, K, T0, iv, is_call))
        hs = half_spread(m, sym)
        credit += max(0.0, m - hs) if q < 0 else -(m + hs)
    if credit <= 0.02:
        return None
    widths = [abs(legs[j][0] - legs[j + 1][0]) for j in range(0, len(legs), 2)]
    max_loss = max(widths) - credit
    n = int((equity * p.risk_per_trade) // (max_loss * 100 + 2 * len(legs) * p.fee_per_contract_side))
    if p.fixed_contracts is not None:
        n = p.fixed_contracts if max_loss * 100 <= equity else 0
    if n < 1:
        return None
    stop_cost = credit * (1 + p.stop_mult) if p.stop_mult is not None else None
    tp_cost = credit * (1 - p.take_profit) if p.take_profit else None
    exit_cost, reason = None, None
    rows = day.loc[i + 1:]
    for _, b in rows.iterrows():
        te = b["ts"] + pd.Timedelta(minutes=5)
        T = _mins_to_close(te) / MIN_PER_YEAR
        To = T + 5 / MIN_PER_YEAR
        o_cost = _close_cost(legs, b["open"], To, iv, sym)
        worst = max(_close_cost(legs, b["high"], T, iv, sym), _close_cost(legs, b["low"], T, iv, sym))
        best = min(_close_cost(legs, b["high"], T, iv, sym), _close_cost(legs, b["low"], T, iv, sym),
                   _close_cost(legs, b["close"], T, iv, sym))
        if stop_cost is not None and o_cost >= stop_cost:
            exit_cost, reason = o_cost, "stop_gap"
        elif stop_cost is not None and worst >= stop_cost:
            exit_cost, reason = stop_cost, "stop"
        elif tp_cost is not None and best <= tp_cost:
            exit_cost, reason = tp_cost, "take_profit"
        if reason:
            break
    if reason is None:
        exit_cost, reason = _intrinsic(legs, day.iloc[-1]["close"]), "expiry"
    fees = 2 * len(legs) * p.fee_per_contract_side if reason != "expiry" else len(legs) * p.fee_per_contract_side
    pnl_per = (credit - exit_cost) * 100 - fees
    return {"symbol": sym, "date": r["ts"].date(), "credit": round(credit, 3), "max_loss": round(max_loss, 3),
            "strikes": "/".join(f"{k:g}{'C' if c else 'P'}" for k, c, _ in legs), "contracts": n,
            "exit_cost": round(exit_cost, 3), "reason": reason, "pnl": round(pnl_per * n, 2),
            "R": round(pnl_per / (max_loss * 100), 3), "iv": iv}


def backtest(bars, iv_table, p: CP, equity=100_000.0):
    out = []
    for s, df in bars.items():
        df = df.copy()
        df["date"] = df["ts"].dt.date
        for d, day in df.groupby("date"):
            if d not in iv_table.index or len(day) < 70:
                continue
            iv = float(iv_table.loc[d, s])
            res = run_day(day.reset_index(drop=True), s, iv, p, equity)
            if res:
                out.append(res)
    return pd.DataFrame(out)


def summarize(tr: pd.DataFrame, equity=100_000.0, n_days=None):
    if tr.empty:
        return {"trades": 0}
    pnl = tr["pnl"]
    w, l = pnl[pnl > 0], pnl[pnl <= 0]
    daily = tr.groupby("date")["pnl"].sum()
    eq = equity + daily.cumsum()
    full = np.zeros(n_days or len(daily))
    full[: len(daily)] = daily.values
    return {"trades": len(tr), "win_rate": round(len(w) / len(tr), 3), "avg_win": round(w.mean(), 1),
            "avg_loss": round(l.mean(), 1) if len(l) else 0.0, "expectancy_$": round(pnl.mean(), 2),
            "expectancy_R": round(tr["R"].mean(), 4),
            "profit_factor": round(w.sum() / -l.sum(), 2) if l.sum() < 0 else np.inf,
            "worst_trade": round(pnl.min(), 0), "total_pnl": round(pnl.sum(), 0),
            "return_pct": round(pnl.sum() / equity * 100, 1),
            "max_dd_pct": round((eq - eq.cummax()).min() / equity * 100, 1),
            "sharpe": round(full.mean() / full.std() * np.sqrt(252), 2) if full.std() > 0 else np.nan}


BARS, IV, DAYS = {}, None, []


def _job(args):
    name, p, syms = args
    tr = backtest({s: BARS[s] for s in syms}, IV, p)
    return name, p, syms, tr


def main(n_days=1000):
    global BARS, IV, DAYS
    import run_backtest as R
    BARS, DAYS = R.load(n_days)
    IV = load_iv_table()
    jobs = []
    orats = CP()  # 20d shorts / 5d wings / 9:45 / 200% stop  (ORATS published setup)
    for syms in (("SPY",), ("QQQ",)):
        jobs.append(("orats_condor_20d_5d_stop2x", orats, syms))
    grid = itertools.product(["condor", "put_spread"], [0.10, 0.15, 0.20],
                             [time(9, 45), time(11, 0)], [1.0, 2.0, None], [None, 0.5])
    for st, sd, en, sm, tp in grid:
        p = replace(orats, structure=st, short_delta=sd, entry=en, stop_mult=sm, take_profit=tp)
        name = f"{st}_d{sd}_e{en.strftime('%H%M')}_s{sm}_tp{tp}"
        for syms in (("SPY",), ("QQQ",)):
            jobs.append((name, p, syms))
    rows, trades = [], {}
    split = DAYS[int(len(DAYS) * 0.6)]
    with Pool(4) as pool:
        for k, (name, p, syms, tr) in enumerate(pool.imap_unordered(_job, jobs)):
            s = summarize(tr, n_days=len(DAYS))
            a = summarize(tr[tr["date"] < split], n_days=int(len(DAYS) * .6)) if not tr.empty else {}
            b = summarize(tr[tr["date"] >= split], n_days=len(DAYS) - int(len(DAYS) * .6)) if not tr.empty else {}
            rows.append({"run": name, "symbol": syms[0], **s,
                         "IS_win_rate": a.get("win_rate"), "IS_expectancy_R": a.get("expectancy_R"),
                         "OOS_win_rate": b.get("win_rate"), "OOS_expectancy_R": b.get("expectancy_R"),
                         "OOS_return_pct": b.get("return_pct"),
                         **{f"p_{k2}": (v.strftime('%H:%M') if isinstance(v, time) else v) for k2, v in asdict(p).items()}})
            trades[(name, syms[0])] = tr
            if name.startswith("orats"):
                print(name, syms, json.dumps({k2: float(v) if isinstance(v, (np.floating, np.integer)) else v for k2, v in s.items()}), flush=True)
            if k % 50 == 0:
                print(f"progress {k}/{len(jobs)}", flush=True)
    df = pd.DataFrame(rows)
    df.to_csv(HERE / "results" / "condor_summary.csv", index=False)
    for sym in ("SPY", "QQQ"):
        trades[("orats_condor_20d_5d_stop2x", sym)].to_csv(HERE / "results" / f"condor_orats_trades_{sym}.csv", index=False)
    hi = df[df["win_rate"] >= 0.80].sort_values("IS_expectancy_R", ascending=False)
    print("\n>=80% win-rate configs ranked by in-sample expectancy:")
    print(hi[["run", "symbol", "trades", "win_rate", "expectancy_R", "profit_factor", "return_pct",
              "max_dd_pct", "IS_expectancy_R", "OOS_win_rate", "OOS_expectancy_R"]].head(20).to_string())
    print("\ncount >=80% WR:", len(hi), " of which positive expectancy:", int((hi["expectancy_R"] > 0).sum()),
          " positive both IS and OOS:", int(((hi["IS_expectancy_R"] > 0) & (hi["OOS_expectancy_R"] > 0)).sum()))


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 1000)
