"""Build 1-minute SPY/QQQ RTH bars from free Hugging Face datasets.

Sources (public, ungated):
  * Ashutosh53/OHLCV-1m     monthly full-market 1-min parquet (used through 2026-03)
  * sujalgawas/trade-1min-stock-5-yrs  1-min bars 2025-03..2026-09 (used after 2026-03)
Output: data/SPY_1m.parquet, data/QQQ_1m.parquet  (ts in America/New_York, RTH only)
"""
import os, subprocess, sys, tempfile
from pathlib import Path
import pandas as pd

OUT = Path(__file__).parent / "data"
SYMS = ["SPY", "QQQ"]
HF = "https://huggingface.co/datasets"


def months(a, b):
    return [p.strftime("%Y-%m") for p in pd.period_range(a, b, freq="M")]


def rth(df):
    df["ts"] = df["ts"].dt.tz_convert("America/New_York")
    m = df["ts"].dt.hour * 60 + df["ts"].dt.minute
    return df[(m >= 570) & (m < 960)]


def get(url, path):
    subprocess.run(["curl", "-sSfL", "--retry", "4", "-m", "900", "-o", str(path), url], check=True)


def main(start="2022-10", end="2026-03"):
    tmp = Path(tempfile.mkdtemp(dir=os.environ.get("SCRATCH", None)))
    parts = []
    for m in months(start, end):
        cache = OUT / f"_part_{m}.parquet"
        if not cache.exists():
            f = tmp / "m.parquet"
            get(f"{HF}/Ashutosh53/OHLCV-1m/resolve/main/data/ohlcv_{m}.parquet", f)
            t = pd.read_parquet(f, filters=[("ticker", "in", SYMS)])
            f.unlink()
            t = t.rename(columns={"timestamp": "ts", "ticker": "symbol"})
            rth(t).to_parquet(cache)
        p = pd.read_parquet(cache)
        print(m, p.groupby("symbol").size().to_dict(), flush=True)
        parts.append(p)
    # tail from second dataset
    for i in (0, 1):
        f = tmp / f"s{i}.parquet"
        get(f"{HF}/sujalgawas/trade-1min-stock-5-yrs/resolve/main/data/train-0000{i}-of-00002.parquet", f)
        t = pd.read_parquet(f, columns=["symbol", "timestamp", "open", "high", "low", "close", "volume"],
                            filters=[("symbol", "in", SYMS)])
        f.unlink()
        t = t.rename(columns={"timestamp": "ts"})
        cutoff = pd.Timestamp(end).to_period("M").end_time.tz_localize("UTC")
        parts.append(rth(t[t["ts"] > cutoff]))
    allp = pd.concat(parts, ignore_index=True)
    for s in SYMS:
        d = allp[allp["symbol"] == s].drop(columns="symbol").drop_duplicates("ts").sort_values("ts")
        d.to_parquet(OUT / f"{s}_1m.parquet")
        print(s, len(d), d["ts"].min(), d["ts"].max(), d["ts"].dt.date.nunique(), "days")


if __name__ == "__main__":
    main(*sys.argv[1:])
