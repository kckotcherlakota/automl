"""VWAP + Opening-Range-Breakout + trend-confirmation 0DTE backtester.

Signal is computed on real 5-minute SPY/QQQ bars. The option leg is modelled
(see options_model.py). Everything that could peek ahead is avoided:
  * opening range = 09:30-09:45 (first three 5-min bars), known at 09:45
  * a signal uses only the CLOSE of a completed bar; entry fills at that close
  * the 20-bar volume average excludes the current bar
  * exits are checked from the NEXT bar on; if stop and target both fall
    inside one bar we assume the stop hit first (conservative)
"""
from __future__ import annotations

from dataclasses import dataclass, field, asdict
from datetime import time

import numpy as np
import pandas as pd

from options_model import MIN_PER_YEAR, bs_price, half_spread, pick_strike


@dataclass
class Params:
    target_pct: float = 0.40          # profit target on premium
    stop_pct: float = 0.20            # stop on premium
    target_delta: float = 0.575       # middle of 0.50-0.65 band
    window_start: time = time(9, 45)  # earliest signal-bar CLOSE is 9:50
    window_end: time = time(11, 30)   # latest signal-bar close
    forced_exit: time = time(15, 0)
    require_retest: bool = True
    retest_tol: float = 0.0005        # low must come within 0.05% of OR edge
    retest_max_bars: int = 6          # retest must happen within 30 min
    require_volume: bool = True
    require_vwap: bool = True
    require_ema: bool = True
    tech_invalidation: bool = True    # exit on close back inside OR / across VWAP
    risk_per_trade: float = 0.005
    daily_loss_limit: float = 0.01
    max_trades_per_day: int = 2
    fee_per_contract_side: float = 0.65
    random_direction: bool = False    # baseline: same timing, coin-flip side
    max_premium: float | None = None  # small accounts: move strike OTM until ask <= this
    fixed_contracts: int | None = None  # bypass risk sizing (e.g. 1 lot for a $100 account)
    seed: int = 7


@dataclass
class Trade:
    symbol: str
    date: object
    side: str
    entry_time: object
    exit_time: object
    strike: float
    entry_px: float
    exit_px: float
    contracts: int
    reason: str
    pnl: float
    ret_pct: float
    iv: float


def add_indicators(df: pd.DataFrame) -> pd.DataFrame:
    df = df.sort_values("ts").reset_index(drop=True)
    df["date"] = df["ts"].dt.date
    df["tod"] = df["ts"].dt.time
    tp = (df["high"] + df["low"] + df["close"]) / 3
    pv = (tp * df["volume"]).groupby(df["date"]).cumsum()
    vv = df["volume"].groupby(df["date"]).cumsum().replace(0, np.nan)
    df["vwap"] = (pv / vv).fillna(df["close"])
    df["ema9"] = df["close"].ewm(span=9, adjust=False).mean()
    df["ema21"] = df["close"].ewm(span=21, adjust=False).mean()
    df["vol20"] = df["volume"].shift(1).rolling(20, min_periods=20).mean()
    return df


def _bar_end_minutes_to_close(ts) -> float:
    end = ts + pd.Timedelta(minutes=5)
    return (16 * 60) - (end.hour * 60 + end.minute)


def simulate_symbol_day(day: pd.DataFrame, symbol: str, iv: float, p: Params,
                        rng: np.random.Generator):
    """Yield candidate entries (bar index, side) for one symbol-day in time order."""
    or_bars = day[day["tod"] < time(9, 45)]
    if len(or_bars) < 3:
        return []
    orh, orl = or_bars["high"].max(), or_bars["low"].min()
    signals = []
    state = {"long": None, "short": None}  # index of breakout bar
    rows = day.reset_index(drop=True)
    for i, r in rows.iterrows():
        bar_close_t = (r["ts"] + pd.Timedelta(minutes=5)).time()
        if r["tod"] < p.window_start:
            continue
        vol_ok = (not p.require_volume) or (pd.notna(r["vol20"]) and r["volume"] > r["vol20"])
        for side in ("long", "short"):
            beyond = r["close"] > orh if side == "long" else r["close"] < orl
            trend_ok = True
            if p.require_vwap:
                trend_ok &= r["close"] > r["vwap"] if side == "long" else r["close"] < r["vwap"]
            if p.require_ema:
                trend_ok &= r["ema9"] > r["ema21"] if side == "long" else r["ema9"] < r["ema21"]
            b = state[side]
            if b is None:
                if beyond and vol_ok:
                    if not p.require_retest:
                        if trend_ok and bar_close_t <= p.window_end:
                            signals.append((i, side))
                    else:
                        state[side] = i
                continue
            # armed: look for a retest
            if i - b > p.retest_max_bars or not beyond:
                state[side] = None if not (beyond and vol_ok) else i
                continue
            if side == "long":
                touched = r["low"] <= orh * (1 + p.retest_tol)
            else:
                touched = r["high"] >= orl * (1 - p.retest_tol)
            if touched and beyond and trend_ok and bar_close_t <= p.window_end:
                signals.append((i, side))
                state[side] = None
    out = []
    for i, side in signals:
        if p.random_direction:
            side = "long" if rng.random() < 0.5 else "short"
        out.append((i, side, orh, orl))
    return out


def run_trade(rows: pd.DataFrame, i: int, side: str, orh: float, orl: float,
              symbol: str, iv: float, p: Params, equity: float):
    is_call = side == "long"
    r = rows.iloc[i]
    S0 = r["close"]
    T0 = _bar_end_minutes_to_close(r["ts"]) / MIN_PER_YEAR
    K = pick_strike(S0, T0, iv, is_call, p.target_delta)
    mid0 = float(bs_price(S0, K, T0, iv, is_call))
    if p.max_premium is not None:
        step = 1.0 if is_call else -1.0
        while mid0 + half_spread(mid0, symbol) > p.max_premium and mid0 > 0.05:
            K += step
            mid0 = float(bs_price(S0, K, T0, iv, is_call))
    if mid0 < 0.05:
        return None
    entry = mid0 + half_spread(mid0, symbol)
    risk_per_contract = entry * p.stop_pct * 100 + 2 * p.fee_per_contract_side
    n = int((equity * p.risk_per_trade) // risk_per_contract)
    if p.fixed_contracts is not None:
        n = p.fixed_contracts if entry * 100 + p.fee_per_contract_side <= equity else 0
    if n < 1:
        return None
    stop_px, tgt_px = entry * (1 - p.stop_pct), entry * (1 + p.target_pct)

    exit_px, reason, exit_idx = None, None, None
    for j in range(i + 1, len(rows)):
        b = rows.iloc[j]
        T = _bar_end_minutes_to_close(b["ts"]) / MIN_PER_YEAR
        Topen = (_bar_end_minutes_to_close(b["ts"]) + 5) / MIN_PER_YEAR

        def bid(S, t):
            m = float(bs_price(S, K, t, iv, is_call))
            return max(0.0, m - half_spread(m, symbol))

        o_bid = bid(b["open"], Topen)
        worst_S, best_S = (b["low"], b["high"]) if is_call else (b["high"], b["low"])
        worst_bid, best_bid = bid(worst_S, T), bid(best_S, T)
        if o_bid <= stop_px:
            exit_px, reason = o_bid, "stop_gap"
        elif worst_bid <= stop_px:
            exit_px, reason = stop_px, "stop"
        elif o_bid >= tgt_px:
            exit_px, reason = o_bid, "target_gap"
        elif best_bid >= tgt_px:
            exit_px, reason = tgt_px, "target"
        else:
            c = b["close"]
            invalid = False
            if p.tech_invalidation:
                if is_call:
                    invalid = c < orh or c < b["vwap"]
                else:
                    invalid = c > orl or c > b["vwap"]
            bar_close_t = (b["ts"] + pd.Timedelta(minutes=5)).time()
            if invalid:
                exit_px, reason = bid(c, T), "invalidated"
            elif bar_close_t >= p.forced_exit:
                exit_px, reason = bid(c, T), "time"
        if reason:
            exit_idx = j
            break
    if reason is None:  # data ended early
        j = len(rows) - 1
        b = rows.iloc[j]
        T = _bar_end_minutes_to_close(b["ts"]) / MIN_PER_YEAR
        m = float(bs_price(b["close"], K, T, iv, is_call))
        exit_px, reason, exit_idx = max(0, m - half_spread(m, symbol)), "eod", j

    pnl = (exit_px - entry) * 100 * n - 2 * p.fee_per_contract_side * n
    return Trade(symbol, r["date"], "CALL" if is_call else "PUT", r["ts"] + pd.Timedelta(minutes=5),
                 rows.iloc[exit_idx]["ts"] + pd.Timedelta(minutes=5), K, round(entry, 3),
                 round(exit_px, 3), n, reason, round(pnl, 2),
                 round(exit_px / entry - 1, 4), round(iv, 4)), exit_idx


def backtest(bars: dict[str, pd.DataFrame], iv_table: pd.DataFrame, p: Params,
             start_equity: float = 100_000.0, compound: bool = False) -> pd.DataFrame:
    """Portfolio backtest across symbols; the per-day trade cap and daily loss
    limit are account-wide. Returns a DataFrame of trades."""
    rng = np.random.default_rng(p.seed)
    data = {s: add_indicators(df) for s, df in bars.items()}
    days = sorted(set().union(*[set(d["date"]) for d in data.values()]))
    equity = start_equity
    trades: list[Trade] = []
    for d in days:
        if d not in iv_table.index:
            continue
        cands = []
        day_rows = {}
        for s, df in data.items():
            day = df[df["date"] == d].reset_index(drop=True)
            if len(day) < 70:  # skip half-days / bad data
                continue
            iv = float(iv_table.loc[d, s])
            if not np.isfinite(iv):
                continue
            day_rows[s] = (day, iv)
            for i, side, orh, orl in simulate_symbol_day(day, s, iv, p, rng):
                cands.append((day.loc[i, "ts"], s, i, side, orh, orl))
        cands.sort(key=lambda x: (x[0], x[1]))
        day_pnl, n_tr, busy_until = 0.0, 0, {}
        for ts, s, i, side, orh, orl in cands:
            if n_tr >= p.max_trades_per_day:
                break
            if day_pnl <= -p.daily_loss_limit * (equity if compound else start_equity):
                break
            if busy_until.get(s, -1) >= i:  # one position per symbol at a time
                continue
            day, iv = day_rows[s]
            res = run_trade(day, i, side, orh, orl, s, iv, p,
                            equity if compound else start_equity)
            if res is None:
                continue
            t, exit_idx = res
            busy_until[s] = exit_idx
            trades.append(t)
            day_pnl += t.pnl
            n_tr += 1
        equity += day_pnl
    return pd.DataFrame([asdict(t) for t in trades])


def summarize(tr: pd.DataFrame, start_equity: float = 100_000.0, n_days: int | None = None) -> dict:
    if tr.empty:
        return {"trades": 0}
    pnl = tr["pnl"]
    wins, losses = pnl[pnl > 0], pnl[pnl <= 0]
    daily = tr.groupby("date")["pnl"].sum()
    eq = start_equity + pnl.cumsum()
    dd = (eq - eq.cummax()).min()
    pf = wins.sum() / -losses.sum() if losses.sum() < 0 else np.inf
    sharpe = daily.mean() / daily.std() * np.sqrt(252) if daily.std() > 0 else np.nan
    if n_days:  # include zero-PnL days in Sharpe
        full = np.zeros(n_days)
        full[: len(daily)] = daily.values
        sharpe = full.mean() / full.std() * np.sqrt(252) if full.std() > 0 else np.nan
    return {
        "trades": int(len(tr)),
        "win_rate": round(len(wins) / len(tr), 3),
        "avg_win": round(wins.mean(), 1) if len(wins) else 0.0,
        "avg_loss": round(losses.mean(), 1) if len(losses) else 0.0,
        "expectancy_$": round(pnl.mean(), 2),
        "expectancy_R": round((tr["pnl"] / (start_equity * 0.005)).mean(), 3),
        "profit_factor": round(pf, 2),
        "total_pnl": round(pnl.sum(), 0),
        "return_pct": round(pnl.sum() / start_equity * 100, 1),
        "max_dd_pct": round(dd / start_equity * 100, 1),
        "sharpe": round(sharpe, 2),
    }
