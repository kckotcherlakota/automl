"""What do these 0DTE strategies do on a $100 account?

Each strategy is run at 1 contract (per-contract P&L), then a sequential
account simulation is done on the LAST 252 trading days:
  * fixed  : always 1 contract while the account can afford it
  * reinvest: contracts = floor(equity / capital needed per contract)
The account is "busted" once it can no longer afford one contract.
Fees: $0.05 per contract per side (Robinhood-style: $0 commission + reg fees).

    python small_account.py
"""
from __future__ import annotations

import json
from dataclasses import replace
from datetime import time
from pathlib import Path

import numpy as np
import pandas as pd

import condor as C
import engine as E
import run_backtest as R
from options_model import load_iv_table

HERE = Path(__file__).parent
FEE = 0.05
START = 100.0


def strategies():
    orb = E.Params(fixed_contracts=1, max_premium=1.00, fee_per_contract_side=FEE,
                   daily_loss_limit=1e9)  # 1-lot: the 1% daily cap can't be expressed
    return {
        "ORB_rules_as_given_(premium<=$1)": ("orb", orb, ("SPY", "QQQ")),
        "IronCondor_SPY_20d_$1wings_0945_stop2x": (
            "condor", C.CP(wing_width=1.0, fixed_contracts=1, fee_per_contract_side=FEE), ("SPY",)),
        "IronCondor_SPY_10d_$1wings_0945_nostop": (
            "condor", C.CP(short_delta=0.10, wing_width=1.0, stop_mult=None, fixed_contracts=1,
                           fee_per_contract_side=FEE), ("SPY",)),
        "PutSpread_SPY_10d_$1wide_0945_nostop": (
            "condor", C.CP(structure="put_spread", short_delta=0.10, wing_width=1.0, stop_mult=None,
                           fixed_contracts=1, fee_per_contract_side=FEE), ("SPY",)),
    }


def per_contract_trades(kind, p, syms, bars, iv):
    b = {s: bars[s] for s in syms}
    if kind == "orb":
        tr = E.backtest(b, iv, p, start_equity=1e9)
        tr["capital"] = tr["entry_px"] * 100 + FEE
    else:
        tr = C.backtest(b, iv, p, equity=1e9)
        tr["capital"] = tr["max_loss"] * 100
    tr["pnl1"] = tr["pnl"] / tr["contracts"]
    return tr.sort_values("date").reset_index(drop=True)


def simulate(tr: pd.DataFrame, days: list, reinvest: bool):
    eq, curve, busted = START, [], None
    by_day = {d: g for d, g in tr.groupby("date")}
    for d in days:
        for _, t in by_day.get(d, pd.DataFrame()).iterrows():
            n = int(eq // t["capital"]) if reinvest else (1 if eq >= t["capital"] else 0)
            eq += n * t["pnl1"]
        if busted is None and eq < tr["capital"].min():
            busted = d
        curve.append((d, eq))
    return pd.DataFrame(curve, columns=["date", "equity"]), busted


def main():
    bars, days = R.load(1000)
    iv = load_iv_table()
    last = days[-252:]
    out = {}
    for name, (kind, p, syms) in strategies().items():
        tr = per_contract_trades(kind, p, syms, bars, iv)
        tr.to_csv(HERE / "results" / f"small_{name}.csv", index=False)
        all_w = tr.groupby(pd.to_datetime(tr["date"]).dt.to_period("W"))["pnl1"].sum()
        t1 = tr[tr["date"].isin(set(last))]
        fx, b1 = simulate(tr, last, False)
        rv, b2 = simulate(tr, last, True)
        wk = fx.assign(w=pd.to_datetime(fx["date"]).dt.to_period("W")).groupby("w")["equity"].last().diff()
        out[name] = {
            "trades_1000d": len(tr), "win_rate": round((tr["pnl1"] > 0).mean(), 3),
            "avg_profit_per_trade_1lot_$": round(tr["pnl1"].mean(), 2),
            "avg_win_$": round(tr.loc[tr.pnl1 > 0, "pnl1"].mean(), 2),
            "avg_loss_$": round(tr.loc[tr.pnl1 <= 0, "pnl1"].mean(), 2),
            "worst_trade_$": round(tr["pnl1"].min(), 2),
            "capital_per_contract_median_$": round(tr["capital"].median(), 0),
            "avg_week_1lot_$_1000d": round(all_w.mean(), 2),
            "pct_weeks_positive_1000d": round((all_w > 0).mean(), 3),
            "last_252d_trades": len(t1), "last_252d_net_1lot_$": round(t1["pnl1"].sum(), 2),
            "last_252d_end_equity_fixed_1lot_$": round(fx["equity"].iloc[-1], 2),
            "last_252d_avg_week_fixed_$": round(wk.mean(), 2),
            "last_252d_end_equity_reinvest_$": round(rv["equity"].iloc[-1], 2),
            "busted_fixed": str(b1) if b1 else None, "busted_reinvest": str(b2) if b2 else None,
        }
        print(name, json.dumps(out[name], indent=1), flush=True)
    (HERE / "results" / "small_account.json").write_text(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
