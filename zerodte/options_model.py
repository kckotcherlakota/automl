"""Black-Scholes pricing for 0DTE SPY/QQQ options.

We have underlying 5-minute bars but no historical option quotes, so option
prices are modelled: BS with r=0, IV taken from Cboe daily indices
(VIX1D for SPY; VXN scaled by VIX1D/VIX for QQQ), time-to-expiry measured in
trading minutes until the 16:00 ET close, plus a bid/ask spread model.
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.stats import norm

MIN_PER_YEAR = 390 * 252
DATA_DIR = Path(__file__).parent / "data"


def bs_price(S, K, T, sigma, is_call: bool):
    S = np.asarray(S, dtype=float)
    T = max(float(T), 1.0 / MIN_PER_YEAR)  # floor at 1 minute
    v = sigma * math.sqrt(T)
    d1 = (np.log(S / K) + 0.5 * v * v) / v
    d2 = d1 - v
    if is_call:
        return S * norm.cdf(d1) - K * norm.cdf(d2)
    return K * norm.cdf(-d2) - S * norm.cdf(-d1)


def bs_delta(S, K, T, sigma, is_call: bool) -> float:
    T = max(float(T), 1.0 / MIN_PER_YEAR)
    v = sigma * math.sqrt(T)
    d1 = (math.log(S / K) + 0.5 * v * v) / v
    return norm.cdf(d1) if is_call else norm.cdf(d1) - 1.0


def pick_strike(S, T, sigma, is_call, target_delta, strike_step=1.0):
    """Strike (on the listed $1 grid) whose |delta| is closest to target."""
    base = round(S / strike_step) * strike_step
    best, best_err = base, 9.0
    for k in range(-30, 31):
        K = base + k * strike_step
        if K <= 0:
            continue
        err = abs(abs(bs_delta(S, K, T, sigma, is_call)) - target_delta)
        if err < best_err:
            best, best_err = K, err
    return best


def half_spread(mid: float, symbol: str) -> float:
    """Approximate half bid/ask for liquid near-the-money 0DTE SPY/QQQ."""
    if symbol == "SPY":
        return max(0.01, 0.0075 * mid)
    return max(0.015, 0.0125 * mid)


def load_iv_table() -> pd.DataFrame:
    """Daily IV inputs keyed by date (uses the day's OPEN print: known ~9:31)."""
    def rd(name):
        df = pd.read_csv(DATA_DIR / f"{name}.csv")
        df["DATE"] = pd.to_datetime(df["DATE"], format="%m/%d/%Y").dt.date
        return df.set_index("DATE")["OPEN"].rename(name)

    t = pd.concat([rd("VIX"), rd("VIX1D"), rd("VXN")], axis=1).sort_index()
    t = t.ffill()
    # VIX1D open prints are noisy in its early history; clamp to sane band
    t["VIX1D"] = t["VIX1D"].clip(lower=5, upper=120)
    t["SPY"] = t["VIX1D"] / 100.0
    t["QQQ"] = (t["VXN"] * (t["VIX1D"] / t["VIX"])).clip(5, 150) / 100.0
    return t[["SPY", "QQQ"]]
