"""$100-account answer: weekly and 1-year results, chosen without look-ahead.

1. For every strategy family, among grid cells a $100 account can afford
   (capital per contract <= $100) pick the best by 1-lot $/trade using ONLY
   the first 60% of days (2022-10 .. 2025-02).
2. Re-run those picks with a forced close at 15:50 ET (no expiry/assignment).
3. Simulate a $100 account (1 contract while affordable, and a reinvest
   variant) separately on each 252-day year.
"""
import subprocess
from pathlib import Path

import numpy as np
import pandas as pd

import fast as F

HERE = Path(__file__).parent
RES = HERE / "results"


def main():
    days = pd.read_csv(RES / "days.csv")["date"].tolist()
    N = len(days)
    split = int(N * 0.6)
    S = pd.read_csv(RES / "fast_summary.csv")
    tr = pd.read_csv(RES / "fast_trades.csv")
    tr["p1"] = tr["pnl"] / tr["contracts"]
    is_ = tr[(tr["day"] < split) & (tr["capital"] <= 100)]
    agg = is_.groupby("id").agg(n=("p1", "size"), avg=("p1", "mean"), cap=("capital", "max"))
    agg = agg[agg["n"] >= 100].join(S.set_index("id")[["name", "family"]])
    agg = agg[~agg["family"].isin(["orb_random_direction"])]
    picks = agg.sort_values("avg", ascending=False).groupby("family").head(1)
    grid = {g["id"]: g for g in F.build_grid()}
    lines, meta = [], []
    for k, (gid, r) in enumerate(picks.iterrows()):
        g = {kk: v for kk, v in grid[gid].items() if kk != "name"}
        g.update(fixed=1, fee=0.05)          # 1 lot, Robinhood-style fees
        if g["kind"] not in ("orb", "calendar"):
            g["exit"] = 950                   # close 15:50, avoid expiry/assignment
        g["id"] = k
        lines.append(F.cfg_line(g))
        meta.append({"id": k, "family": r["family"], "picked": r["name"], "IS_avg_1lot_$": round(r["avg"], 2)})
    # the user's own rules at $100 (premium capped so 1 contract is affordable)
    base = dict(kind="orb", target=0.4, stop=0.2, delta=0.575, wstart=585, wend=690, fexit=900, retest=1,
                vol=1, vwap=1, ema=1, tech=1, dll=1e9, maxtr=2, fee=0.05, fixed=1, maxprem=1.0, syms=3)
    lines.append(F.cfg_line({"id": len(meta), **base}))
    meta.append({"id": len(meta), "family": "orb_rules_as_given", "picked": "your rules, premium<=$1", "IS_avg_1lot_$": None})
    cfg = RES / "final100_cfg.txt"
    cfg.write_text("\n".join(lines) + "\n")
    subprocess.run([str(F.BIN), str(RES / "bars_5m.csv"), str(cfg), str(RES / "final100_trades.csv")], check=True)
    t = pd.read_csv(RES / "final100_trades.csv")
    t["p1"] = t["pnl"] / t["contracts"]
    windows = {"Y1 2022-10..2023-09": range(0, 252), "Y2 2023-10..2024-09": range(252, 504),
               "Y3 2024-10..2025-09": range(504, 756), "Y4 2025-10..2026-09*": range(N - 252, N)}
    rows = []
    for m in meta:
        x = t[t["id"] == m["id"]]
        row = dict(m)
        row["trades_1000d"] = len(x)
        row["win_rate"] = round((x["p1"] > 0).mean(), 3)
        row["avg_per_trade_$"] = round(x["p1"].mean(), 2)
        row["OOS_avg_per_trade_$"] = round(x[x["day"] >= split]["p1"].mean(), 2)
        row["worst_trade_$"] = round(x["p1"].min(), 2)
        for wn, rg in windows.items():
            curve, busted = F.sim_small(x, list(rg), start=100.0)
            y = x[x["day"].isin(rg)]
            wk = pd.Series(y["p1"].values, index=pd.to_datetime([days[d] for d in y["day"]])).resample("W").sum()
            row[f"{wn} net_1lot_$"] = round(y["p1"].sum(), 0)
            row[f"{wn} avg_week_$"] = round(wk.mean(), 1)
            row[f"{wn} $100_acct_end"] = round(curve[-1], 0)
            row[f"{wn} busted"] = days[busted] if busted is not None else ""
        rows.append(row)
    out = pd.DataFrame(rows)
    out.to_csv(RES / "final100.csv", index=False)
    pd.set_option("display.width", 300)
    pd.set_option("display.max_columns", 50)
    print(out.T.to_string())


if __name__ == "__main__":
    main()
