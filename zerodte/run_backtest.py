"""Run the VWAP + ORB + trend 0DTE study on the last N trading days of SPY/QQQ.

    python run_backtest.py [n_days]

Writes results/ (trades + summary CSVs). See REPORT.md for interpretation.
"""
from __future__ import annotations

import itertools
import json
import sys
from dataclasses import replace, asdict
from datetime import time
from multiprocessing import Pool
from pathlib import Path

import numpy as np
import pandas as pd

from engine import Params, backtest, summarize
from options_model import load_iv_table

HERE = Path(__file__).parent
RES = HERE / "results"
RES.mkdir(exist_ok=True)
BARS: dict[str, pd.DataFrame] = {}
IV = None
N_DAYS = 0


def to_5m(df1: pd.DataFrame) -> pd.DataFrame:
    g = df1.set_index("ts").resample("5min", label="left", closed="left")
    out = pd.DataFrame({"open": g["open"].first(), "high": g["high"].max(), "low": g["low"].min(),
                        "close": g["close"].last(), "volume": g["volume"].sum()}).dropna()
    out = out.reset_index()
    m = out["ts"].dt.hour * 60 + out["ts"].dt.minute
    return out[(m >= 570) & (m < 960)].reset_index(drop=True)


def load(n_days: int):
    bars = {}
    for s in ("SPY", "QQQ"):
        d = to_5m(pd.read_parquet(HERE / "data" / f"{s}_1m.parquet"))
        bars[s] = d
    common = sorted(set(bars["SPY"]["ts"].dt.date) & set(bars["QQQ"]["ts"].dt.date))
    # keep full sessions only, then last n_days
    full = [d for d in common
            if (bars["SPY"]["ts"].dt.date == d).sum() >= 70 and (bars["QQQ"]["ts"].dt.date == d).sum() >= 70]
    keep = set(full[-n_days:])
    return {s: b[b["ts"].dt.date.isin(keep)].reset_index(drop=True) for s, b in bars.items()}, sorted(keep)


def _run(args):
    name, p, syms, day_subset = args
    bars = {s: BARS[s] for s in syms}
    if day_subset is not None:
        bars = {s: b[b["ts"].dt.date.isin(day_subset)] for s, b in bars.items()}
    tr = backtest(bars, IV, p)
    n = len(day_subset) if day_subset is not None else N_DAYS
    return name, p, syms, tr, summarize(tr, n_days=n)


def fmt(p: Params) -> dict:
    d = asdict(p)
    return {k: (v.strftime("%H:%M") if isinstance(v, time) else v) for k, v in d.items()}


def main(n_days=1000):
    global BARS, IV, N_DAYS
    BARS, days = load(n_days)
    N_DAYS = len(days)
    IV = load_iv_table()
    print(f"days: {N_DAYS}  {days[0]} -> {days[-1]}", flush=True)
    base = Params()
    jobs = []
    # 1) the exact proposed rules
    for syms in (("SPY",), ("QQQ",), ("SPY", "QQQ")):
        jobs.append(("rules_as_given", base, syms, None))
    # 2) control: same entry timing, coin-flip direction
    for seed in range(5):
        jobs.append((f"random_dir_seed{seed}", replace(base, random_direction=True, seed=seed),
                     ("SPY", "QQQ"), None))
    # 3) ablations (one filter removed at a time)
    abl = {"no_retest": dict(require_retest=False), "no_volume": dict(require_volume=False),
           "no_vwap": dict(require_vwap=False), "no_ema": dict(require_ema=False),
           "no_tech_exit": dict(tech_invalidation=False)}
    for k, kw in abl.items():
        jobs.append((k, replace(base, **kw), ("SPY", "QQQ"), None))
    # 4) exit / contract grid
    grid = list(itertools.product([0.25, 0.40, 0.60, 1.00], [0.20, 0.30, 0.50],
                                  [0.40, 0.575, 0.70], [True, False], [time(11, 30), time(13, 0)]))
    for tg, st, dl, te, we in grid:
        p = replace(base, target_pct=tg, stop_pct=st, target_delta=dl, tech_invalidation=te, window_end=we)
        jobs.append((f"grid_t{tg}_s{st}_d{dl}_te{int(te)}_w{we.strftime('%H%M')}", p, ("SPY", "QQQ"), None))

    rows, trades = [], {}
    with Pool(4) as pool:
        for i, (name, p, syms, tr, summ) in enumerate(pool.imap_unordered(_run, jobs)):
            key = f"{name}|{'+'.join(syms)}"
            rows.append({"run": name, "symbols": "+".join(syms), **summ, **fmt(p)})
            trades[key] = tr
            if not name.startswith("grid"):
                print(key, json.dumps({k: (float(v) if isinstance(v, (np.floating,)) else v) for k, v in summ.items()}), flush=True)
            elif i % 20 == 0:
                print(f"  grid progress {i}/{len(jobs)}", flush=True)
    summ = pd.DataFrame(rows)
    summ.to_csv(RES / "summary_all.csv", index=False)
    for key in ("rules_as_given|SPY+QQQ", "rules_as_given|SPY", "rules_as_given|QQQ"):
        trades[key].to_csv(RES / f"trades_{key.replace('|', '_').replace('+', '_')}.csv", index=False)

    # 5) walk-forward: choose best grid cell on first 60% of days, evaluate on last 40%
    split = int(N_DAYS * 0.6)
    is_days, oos_days = set(days[:split]), set(days[split:])
    g = summ[summ["run"].str.startswith("grid")].copy()
    wf = []
    for _, r in g.iterrows():
        tr = trades[f"{r['run']}|SPY+QQQ"]
        if tr.empty:
            continue
        a, b = tr[tr["date"].isin(is_days)], tr[~tr["date"].isin(is_days)]
        wf.append({"run": r["run"], **{f"IS_{k}": v for k, v in summarize(a, n_days=len(is_days)).items()},
                   **{f"OOS_{k}": v for k, v in summarize(b, n_days=len(oos_days)).items()}})
    wf = pd.DataFrame(wf).sort_values("IS_expectancy_$", ascending=False)
    wf.to_csv(RES / "walk_forward.csv", index=False)
    best = wf.iloc[0]["run"]
    trades[f"{best}|SPY+QQQ"].to_csv(RES / "trades_best_in_sample.csv", index=False)
    by_year = {}
    for key in ("rules_as_given|SPY+QQQ", f"{best}|SPY+QQQ"):
        t = trades[key].copy()
        t["year"] = pd.to_datetime(t["date"]).dt.year
        by_year[key] = {int(y): summarize(x) for y, x in t.groupby("year")}
    pd.DataFrame({k: {y: v.get("expectancy_$") for y, v in d.items()} for k, d in by_year.items()}).to_csv(
        RES / "expectancy_by_year.csv")
    meta = {"n_days": N_DAYS, "first_day": str(days[0]), "last_day": str(days[-1]),
            "split_day": str(days[split]), "best_in_sample": best}
    (RES / "meta.json").write_text(json.dumps(meta, indent=2))
    print(json.dumps(meta, indent=2))
    print(wf.head(10).to_string())


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 1000)
