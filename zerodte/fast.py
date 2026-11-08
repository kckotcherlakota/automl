"""Driver for the C++ simulator (cpp/fastsim).

    python fast.py prep      # build results/bars_5m.csv from data/*_1m.parquet + Cboe IV
    python fast.py run       # full grid: all strategy families, summaries, walk-forward, $100 sim

Strategy families
  orb          your VWAP + ORB + EMA rules, buying a 0.50-0.65 delta 0DTE call/put
  orb_spread   same ORB signal, but SELL a credit spread in the signal direction
  condor       0DTE iron condor at a fixed clock time (ORATS-style)
  put_spread / call_spread   0DTE vertical credit spread at a fixed time
  trend_spread credit spread whose side follows VWAP + EMA9/21 trend at entry
  calendar     short 0DTE ATM / long next-day ATM (same strike), closed same day
"""
from __future__ import annotations

import itertools
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

HERE = Path(__file__).parent
RES = HERE / "results"
BIN = HERE / "cpp" / "fastsim"
SYMS = {"SPY": 0, "QQQ": 1}
START_EQ = 100_000.0


def iv_table() -> pd.DataFrame:
    def rd(n):
        d = pd.read_csv(HERE / "data" / f"{n}.csv")
        d["DATE"] = pd.to_datetime(d["DATE"], format="%m/%d/%Y").dt.date
        return d.set_index("DATE")["OPEN"].rename(n)
    t = pd.concat([rd(n) for n in ("VIX", "VIX1D", "VIX9D", "VXN")], axis=1).sort_index().ffill()
    t["VIX1D"] = t["VIX1D"].clip(5, 120)
    out = pd.DataFrame(index=t.index)
    out["SPY_f"] = t["VIX1D"] / 100
    out["SPY_b"] = t["VIX9D"] / 100
    out["QQQ_f"] = (t["VXN"] * t["VIX1D"] / t["VIX"]).clip(5, 150) / 100
    out["QQQ_b"] = (t["VXN"] * t["VIX9D"] / t["VIX"]).clip(5, 150) / 100
    return out


def prep(n_days=1000):
    import run_backtest as R
    from engine import add_indicators
    bars, days = R.load(n_days)
    iv = iv_table()
    di = {d: k for k, d in enumerate(days)}
    parts = []
    for s, sid in SYMS.items():
        b = add_indicators(bars[s])
        b["sym"] = sid
        b["day"] = b["date"].map(di)
        b["bar_min"] = b["ts"].dt.hour * 60 + b["ts"].dt.minute
        b["iv_front"] = b["date"].map(iv[f"{s}_f"])
        b["iv_back"] = b["date"].map(iv[f"{s}_b"])
        b["vol20"] = b["vol20"].fillna(-1)
        parts.append(b[["sym", "day", "bar_min", "open", "high", "low", "close", "volume", "vwap",
                        "ema9", "ema21", "vol20", "iv_front", "iv_back"]].dropna())
    out = pd.concat(parts).sort_values(["sym", "day", "bar_min"])
    out.to_csv(RES / "bars_5m.csv", index=False, float_format="%.10g")
    pd.Series([str(d) for d in days]).to_csv(RES / "days.csv", index=False, header=["date"])
    print("wrote", len(out), "rows,", len(days), "days")


def cfg_line(d: dict) -> str:
    return " ".join(f"{k}={v}" for k, v in d.items())


def build_grid():
    G = []

    def add(name, **kw):
        G.append({"id": len(G), "name": name, **kw})

    base_orb = dict(kind="orb", target=0.4, stop=0.2, delta=0.575, wstart=585, wend=690, fexit=900,
                    retest=1, vol=1, vwap=1, ema=1, tech=1, risk=0.005, dll=0.01, maxtr=2, fee=0.65)
    for sm, tag in ((3, "SPY+QQQ"), (1, "SPY"), (2, "QQQ")):
        add(f"orb_rules_as_given|{tag}", **base_orb, syms=sm)
    for seed in range(10):
        add(f"orb_random_direction_s{seed}|SPY+QQQ", **base_orb, syms=3, rnd=1, seed=seed)
    for tg, st, dl, te, we in itertools.product([0.25, 0.4, 0.6, 1.0], [0.2, 0.3, 0.5],
                                                [0.4, 0.575, 0.7], [1, 0], [690, 780]):
        add(f"orb_t{tg}_s{st}_d{dl}_tech{te}_w{we}|SPY+QQQ",
            **{**base_orb, "target": tg, "stop": st, "delta": dl, "tech": te, "wend": we}, syms=3)
    # ORB signal expressed as a credit spread (sell put spread on bull signal)
    for sd, w, sm_, tp, te in itertools.product([0.2, 0.3, 0.4], [1, 2], [1.0, 2.0, -1], [-1, 0.5], [0, 1]):
        add(f"orb_spread_d{sd}_w{w}_stop{sm_}_tp{tp}_tech{te}|SPY+QQQ",
            **{**base_orb, "kind": "orb_spread", "sdelta": sd, "wing": w, "smult": sm_, "tp": tp,
               "tech": te, "risk": 0.01}, syms=3)
    # scheduled premium selling (condor / verticals / trend-directed verticals)
    entries = {"0945": 585, "1100": 660, "1300": 780, "1430": 870}
    for kind, sd, wing, en, sm_, tp, sym in itertools.product(
            ["condor", "put_spread", "call_spread", "trend_spread"], [0.05, 0.10, 0.15, 0.20, 0.30],
            [0, 1, 2], entries, [1.0, 2.0, -1], [-1, 0.5], (1, 2)):
        add(f"{kind}_d{sd}_w{'5d' if wing == 0 else wing}_e{en}_stop{sm_}_tp{tp}|{'SPY' if sym == 1 else 'QQQ'}",
            kind=kind, sdelta=sd, ldelta=0.05 if sd > 0.05 else 0.02, wing=wing, entry=entries[en],
            smult=sm_, tp=tp, risk=0.01, fee=0.65, syms=sym)
    # calendars
    for right, koff, en, stop, tp, ex, sym in itertools.product(
            ["C", "P"], [0, 1, 2], [585, 660, 780, 870], [-1, 0.25, 0.5], [-1, 0.25, 0.5], [930, 950], (1, 2)):
        add(f"calendar_{right}_koff{koff}_e{en}_stop{stop}_tp{tp}_x{ex}|{'SPY' if sym == 1 else 'QQQ'}",
            kind="calendar", right=right, koff=koff, entry=en, stop=stop, tp=tp, exit=ex, risk=0.01,
            fee=0.65, syms=sym)
    return G


def summarize(tr: pd.DataFrame, n_days: int) -> dict:
    if tr.empty:
        return {"trades": 0}
    p = tr["pnl"]
    w, l = p[p > 0], p[p <= 0]
    daily = tr.groupby("day")["pnl"].sum().reindex(range(n_days), fill_value=0.0) if n_days else tr.groupby("day")["pnl"].sum()
    eq = START_EQ + daily.cumsum()
    pnl1 = tr["pnl"] / tr["contracts"]
    return {"trades": len(tr), "win_rate": round(len(w) / len(tr), 3),
            "avg_pnl_$": round(p.mean(), 2), "avg_win_$": round(w.mean(), 2) if len(w) else 0.0,
            "avg_loss_$": round(l.mean(), 2) if len(l) else 0.0,
            "profit_factor": round(w.sum() / -l.sum(), 3) if l.sum() < 0 else 99.0,
            "total_pnl_$": round(p.sum(), 0), "return_pct": round(p.sum() / START_EQ * 100, 1),
            "max_dd_pct": round((eq - eq.cummax()).min() / START_EQ * 100, 1),
            "sharpe": round(daily.mean() / daily.std() * np.sqrt(252), 2) if daily.std() > 0 else 0.0,
            "avg_pnl_1lot_$": round(pnl1.mean(), 2),
            "avg_capital_1lot_$": round(tr["capital"].mean(), 0),
            "ret_on_capital_per_trade_pct": round((pnl1 / tr["capital"]).mean() * 100, 2)}


def sim_small(tr: pd.DataFrame, day_ids, start=100.0, reinvest=False):
    """Sequential small-account sim on per-contract P&L. Returns (equity curve, busted day)."""
    t = tr.assign(pnl1=tr["pnl"] / tr["contracts"]).sort_values(["day", "entry_min"])
    g = {d: x for d, x in t.groupby("day")}
    eq, curve, busted = start, [], None
    for d in day_ids:
        if d in g:
            for _, r in g[d].iterrows():
                n = int(eq // r["capital"]) if reinvest else (1 if eq >= r["capital"] else 0)
                eq += n * r["pnl1"]
        if busted is None and eq < t["capital"].min():
            busted = d
        curve.append(eq)
    return np.array(curve), busted


def run():
    days = pd.read_csv(RES / "days.csv")["date"].tolist()
    N = len(days)
    grid = build_grid()
    cfg = RES / "configs.txt"
    cfg.write_text("\n".join(cfg_line({k: v for k, v in g.items() if k != "name"}) for g in grid) + "\n")
    import time as tm
    t0 = tm.time()
    subprocess.run([str(BIN), str(RES / "bars_5m.csv"), str(cfg), str(RES / "fast_trades.csv")], check=True)
    print(f"C++ sim: {len(grid)} configs in {tm.time() - t0:.1f}s", flush=True)
    tr = pd.read_csv(RES / "fast_trades.csv")
    names = {g["id"]: g["name"] for g in grid}
    split = int(N * 0.6)
    last252 = list(range(N - 252, N))
    rows = []
    for gid, x in tr.groupby("id"):
        s = summarize(x, N)
        a = summarize(x[x["day"] < split], split)
        b = summarize(x[x["day"] >= split], N - split)
        y = x[x["day"] >= N - 252]
        rows.append({"id": gid, "name": names[gid], "family": names[gid].split("_d")[0].split("_t")[0].split("|")[0]
                     if not names[gid].startswith("orb_spread") else "orb_spread", **s,
                     "IS_avg_pnl_$": a.get("avg_pnl_$"), "IS_win_rate": a.get("win_rate"),
                     "OOS_avg_pnl_$": b.get("avg_pnl_$"), "OOS_win_rate": b.get("win_rate"),
                     "OOS_return_pct": b.get("return_pct"),
                     "last252_net_1lot_$": round((y["pnl"] / y["contracts"]).sum(), 2)})
    S = pd.DataFrame(rows)
    fam = S["name"].str.extract(r"^(orb_rules_as_given|orb_random_direction|orb_spread|orb|condor|put_spread|call_spread|trend_spread|calendar)")[0]
    S["family"] = fam
    S.to_csv(RES / "fast_summary.csv", index=False)

    # walk-forward: per family pick best IS (by avg $/trade, >=100 IS trades) -> report OOS
    wf = []
    for f, x in S[~S["family"].isin(["orb_rules_as_given", "orb_random_direction"])].groupby("family"):
        x = x[x["trades"] >= 150].sort_values("IS_avg_pnl_$", ascending=False)
        if x.empty:
            continue
        r = x.iloc[0]
        wf.append({"family": f, "picked_in_sample": r["name"], "IS_avg_pnl_$": r["IS_avg_pnl_$"],
                   "IS_win_rate": r["IS_win_rate"], "OOS_avg_pnl_$": r["OOS_avg_pnl_$"],
                   "OOS_win_rate": r["OOS_win_rate"], "OOS_return_pct": r["OOS_return_pct"],
                   "cells": len(x), "cells_positive_full_period": int((x["avg_pnl_$"] > 0).sum())})
    WF = pd.DataFrame(wf)
    WF.to_csv(RES / "fast_walk_forward.csv", index=False)

    # $100 account, last 252 trading days, 1 contract per trade (affordable ones only)
    small = []
    cands = S[(S["trades"] >= 150) & (S["avg_capital_1lot_$"] <= 100)]
    for _, r in cands.iterrows():
        x = tr[tr["id"] == r["id"]]
        x = x[x["capital"] <= 100]
        if x.empty:
            continue
        curve, busted = sim_small(x, last252)
        yr = x[x["day"] >= N - 252]
        p1 = yr["pnl"] / yr["contracts"]
        weekly = (pd.Series(p1.values, index=pd.to_datetime([days[d] for d in yr["day"]]))
                  .resample("W").sum())
        small.append({"name": r["name"], "family": r["family"], "win_rate_1000d": r["win_rate"],
                      "avg_pnl_1lot_$_1000d": r["avg_pnl_1lot_$"], "trades_last252": len(yr),
                      "net_1lot_last252_$": round(p1.sum(), 2),
                      "avg_week_1lot_$": round(weekly.mean(), 2),
                      "best_week_$": round(weekly.max(), 2), "worst_week_$": round(weekly.min(), 2),
                      "pct_weeks_positive": round((weekly > 0).mean(), 3),
                      "acct100_end_equity_$": round(curve[-1], 2),
                      "acct100_busted": days[busted] if busted is not None else None,
                      "OOS_avg_pnl_$": r["OOS_avg_pnl_$"]})
    SM = pd.DataFrame(small).sort_values("net_1lot_last252_$", ascending=False)
    SM.to_csv(RES / "fast_small_account.csv", index=False)

    pd.set_option("display.width", 250)
    print("\n== reference runs")
    print(S[S["family"].isin(["orb_rules_as_given"])][["name", "trades", "win_rate", "avg_pnl_$", "profit_factor", "return_pct", "max_dd_pct"]].to_string(index=False))
    rd = S[S["family"] == "orb_random_direction"]
    print("random-direction control: avg $/trade mean", round(rd["avg_pnl_$"].mean(), 2), "range", rd["avg_pnl_$"].min(), rd["avg_pnl_$"].max())
    print("\n== family overview (all grid cells)")
    print(S.groupby("family").agg(cells=("id", "size"), pos_cells=("avg_pnl_$", lambda v: int((v > 0).sum())),
                                  best_avg=("avg_pnl_$", "max"), median_avg=("avg_pnl_$", "median"),
                                  max_wr=("win_rate", "max")).to_string())
    print("\n== walk-forward (best in-sample per family -> out-of-sample)")
    print(WF.to_string(index=False))
    hi = S[(S["win_rate"] >= 0.8) & (S["trades"] >= 150)]
    print(f"\n== >=80% win-rate cells: {len(hi)}; positive full-period: {(hi['avg_pnl_$'] > 0).sum()}; "
          f"positive IS and OOS: {((hi['IS_avg_pnl_$'] > 0) & (hi['OOS_avg_pnl_$'] > 0)).sum()}")
    print(hi.sort_values("avg_pnl_$", ascending=False)[["name", "trades", "win_rate", "avg_pnl_$", "avg_win_$", "avg_loss_$", "profit_factor", "return_pct", "max_dd_pct", "OOS_avg_pnl_$"]].head(12).to_string(index=False))
    print("\n== $100 account, last 252 days, 1 contract (top 12 by net)")
    print(SM.head(12).to_string(index=False))


if __name__ == "__main__":
    {"prep": prep, "run": run}[sys.argv[1]]()
