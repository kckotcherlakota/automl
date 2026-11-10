"""V3 Strategies B (SMT sweep reversal at frozen ORH/ORL) and C (trend-filtered SMT pullback, prior-20-bar
levels). Deterministic, completed 1-minute bars only. docs/MICRO_200_V3_RULEBOOK.md §2, §4, §5.
Usage: python3 -I strat_smt.py <1min_json_with_SPY_and_QQQ> <ema_state.json> [YYYY-MM-DD]
"""
import json, sys, statistics
from datetime import datetime, timezone, timedelta

ET = timezone(timedelta(hours=-5))
day = sys.argv[3] if len(sys.argv) > 3 else datetime.now(ET).strftime("%Y-%m-%d")
ENTRY_START, ENTRY_END = 10 * 60, 11 * 60 + 30
raw = json.load(open(sys.argv[1])); ema0 = json.load(open(sys.argv[2]))
D = {}
for r in raw["data"]["results"]:
    bars = {}
    for b in r["bars"]:
        if b.get("interpolated"):
            continue
        t = datetime.fromisoformat(b["begins_at"].replace("Z", "+00:00")).astimezone(ET)
        if t.strftime("%Y-%m-%d") != day:
            continue
        m = t.hour * 60 + t.minute
        bars[m] = dict(m=m, o=float(b["open_price"]), h=float(b["high_price"]), l=float(b["low_price"]),
                       c=float(b["close_price"]), v=float(b["volume"]))
    D[r["symbol"]] = bars
syms = ["SPY", "QQQ"]
mins = sorted(set(D["SPY"]) & set(D["QQQ"]))  # synchronized minutes only
out = {"day": day, "last_sync_bar": (f"{mins[-1]//60}:{mins[-1]%60:02d}" if mins else None), "events": [], "signals": []}
if len([m for m in mins if 570 <= m <= 584]) < 15:
    out["status"] = "OR_NOT_READY"; print(json.dumps(out, indent=1)); sys.exit()
info = {}
for s in syms:
    seq = [D[s][m] for m in mins]
    pv = vv = 0.0
    for b in seq:
        pv += (b["h"] + b["l"] + b["c"]) / 3 * b["v"]; vv += b["v"]; b["vwap"] = pv / vv
    e9, e20 = ema0[s]["ema9"], ema0[s]["ema20"]; ema_at = {}
    for st in range(570, 960, 5):
        if not all((st + k) in D[s] for k in range(5)):
            break
        c = D[s][st + 4]["c"]; e9 += 2 / 10 * (c - e9); e20 += 2 / 21 * (c - e20); ema_at[st + 5] = (e9, e20)
    orb = [D[s][m] for m in range(570, 585)]
    info[s] = dict(seq=seq, ORH=max(b["h"] for b in orb), ORL=min(b["l"] for b in orb), ema_at=ema_at)
    out[s] = dict(ORH=info[s]["ORH"], ORL=info[s]["ORL"], vwap=round(seq[-1]["vwap"], 3),
                  ema9=round(e9, 3), ema20=round(e20, 3), last=seq[-1]["c"])

def ema_known(s, m):  # latest 5-min EMA pair known at the close of 1-min bar m
    ks = [k for k in info[s]["ema_at"] if k <= m + 1]
    return info[s]["ema_at"][max(ks)] if ks else None

def tr(seq, i):
    pc = seq[i - 1]["c"] if i > 0 else seq[i]["o"]
    return max(seq[i]["h"], pc) - min(seq[i]["l"], pc)

def follow(A, i, bull, sweep_ext, strat):
    """MSS + displacement within 8 bars, FVG within 4 bars of MSS, midpoint retest within 5 bars."""
    seq = info[A]["seq"]
    if i < 23:
        return None, "insufficient history"
    pre = seq[i - 3:i]
    lvl = max(b["h"] for b in pre) if bull else min(b["l"] for b in pre)
    k = None
    for j in range(i + 1, min(i + 9, len(seq))):
        b = seq[j]
        if (bull and b["l"] < sweep_ext) or (not bull and b["h"] > sweep_ext):
            return None, f"sweep extreme re-breached at {b['m']//60}:{b['m']%60:02d}"
        if b["m"] + 1 > ENTRY_END:
            return None, "MSS after 11:30"
        if (bull and b["c"] > lvl) or (not bull and b["c"] < lvl):
            rng = b["h"] - b["l"]
            med = statistics.median(tr(seq, x) for x in range(j - 20, j))
            if rng > 0 and abs(b["c"] - b["o"]) / rng >= 0.60 and tr(seq, j) >= 1.20 * med and \
               ((bull and b["c"] > b["o"]) or (not bull and b["c"] < b["o"])):
                k = j; break
    if k is None:
        return None, "no MSS+displacement within 8 bars" if len(seq) >= i + 9 else "waiting for MSS"
    for f in range(k, min(k + 5, len(seq))):
        if f < 2:
            continue
        a, c = seq[f - 2], seq[f]
        gap = (c["l"] - a["h"]) if bull else (a["l"] - c["h"])
        if gap >= 0.0001 * c["c"]:
            mid = (c["l"] + a["h"]) / 2 if bull else (a["l"] + c["h"]) / 2
            for j in range(f + 1, min(f + 6, len(seq))):
                b = seq[j]
                if (bull and b["l"] < sweep_ext) or (not bull and b["h"] > sweep_ext):
                    return None, "sweep extreme re-breached before entry"
                if (bull and b["l"] <= mid < b["c"]) or (not bull and b["h"] >= mid > b["c"]):
                    t = b["m"] + 1
                    if strat == "C":
                        e = ema_known(A, b["m"])
                        if not e or (bull and not (b["c"] > b["vwap"] and e[0] > e[1])) or \
                           (not bull and not (b["c"] < b["vwap"] and e[0] < e[1])):
                            return None, "C trend/VWAP filter failed at confirmation"
                    if not (ENTRY_START <= t <= ENTRY_END):
                        return None, f"confirmation {t//60}:{t%60:02d} outside entry window"
                    return dict(time=f"{t//60}:{t%60:02d}", underlying=b["c"], fvg_mid=round(mid, 3),
                                mss=f"{seq[k]['m']//60}:{seq[k]['m']%60:02d}", sweep_extreme=sweep_ext), "SIGNAL"
            return None, "FVG formed, waiting/no retest within 5 bars" if len(seq) >= f + 6 else "FVG formed, waiting retest"
    return None, "no FVG within 4 bars of MSS" if len(seq) >= k + 5 else "MSS ok, waiting FVG"

seen = set()
for i, m in enumerate(mins):
    if m < 585 or m + 1 > ENTRY_END:
        continue
    for A, B in (("SPY", "QQQ"), ("QQQ", "SPY")):
        a, b = info[A]["seq"][i], info[B]["seq"][i]
        cands = []
        # Strategy B: frozen OR levels; B must not have breached its own level since 09:45
        b_low_since = min(x["l"] for x in info[B]["seq"][15:i]) if i > 15 else 9e9
        b_high_since = max(x["h"] for x in info[B]["seq"][15:i]) if i > 15 else -9e9
        if a["l"] <= info[A]["ORL"] * (1 - 0.0002) and a["c"] > info[A]["ORL"] and \
           b["l"] > info[B]["ORL"] * (1 - 0.0001) and b_low_since > info[B]["ORL"] * (1 - 0.0001):
            cands.append(("B", True, info[A]["ORL"], a["l"]))
        if a["h"] >= info[A]["ORH"] * (1 + 0.0002) and a["c"] < info[A]["ORH"] and \
           b["h"] < info[B]["ORH"] * (1 + 0.0001) and b_high_since < info[B]["ORH"] * (1 + 0.0001):
            cands.append(("B", False, info[A]["ORH"], a["h"]))
        # Strategy C: prior-20-bar levels, trend filter
        if i >= 20:
            ra = info[A]["seq"][i - 20:i]; rb = info[B]["seq"][i - 20:i]
            e = ema_known(A, m - 1)
            if e and e[0] > e[1] and a["c"] > a["vwap"]:
                la, lb = min(x["l"] for x in ra), min(x["l"] for x in rb)
                if a["l"] <= la * (1 - 0.0002) and a["c"] > la and b["l"] > lb * (1 - 0.0001):
                    cands.append(("C", True, la, a["l"]))
            if e and e[0] < e[1] and a["c"] < a["vwap"]:
                ha, hb = max(x["h"] for x in ra), max(x["h"] for x in rb)
                if a["h"] >= ha * (1 + 0.0002) and a["c"] < ha and b["h"] < hb * (1 + 0.0001):
                    cands.append(("C", False, ha, a["h"]))
        for strat, bull, ref, ext in cands:
            key = (strat, A, bull, round(ref, 3))
            if key in seen:
                continue
            seen.add(key)
            sig, why = follow(A, i, bull, ext, strat)
            tag = f"{strat} {'BULL' if bull else 'BEAR'} SMT {A} vs {B} sweep {m//60}:{m%60:02d} ref {round(ref,3)} ext {ext}"
            out["events"].append(f"{tag}: {why}")
            if sig:
                sig.update(strategy=strat, side="CALL" if bull else "PUT", symbol=A, ref=round(ref, 3))
                out["signals"].append(sig)
print(json.dumps(out, indent=1))
