"""V3 Strategy A (ORB + VWAP trend continuation) — deterministic signal evaluation on completed bars.
Usage: python3 -I strat_a.py <1min_bars_json_file> <ema_state.json> [YYYY-MM-DD]
Input file: Robinhood get_equity_historicals JSON (interval=minute) containing SPY and QQQ.
Prints a JSON status per symbol. Rules: docs/MICRO_200_V3_RULEBOOK.md §2-3 (CPI day: entries >= 10:00).
"""
import json, sys
from datetime import datetime, timezone, timedelta

ET = timezone(timedelta(hours=-5))  # EST in November
day = sys.argv[3] if len(sys.argv) > 3 else datetime.now(ET).strftime("%Y-%m-%d")
ENTRY_START, ENTRY_END = 10 * 60, 11 * 60 + 30  # minutes after midnight ET
raw = json.load(open(sys.argv[1]))
ema0 = json.load(open(sys.argv[2]))
res = {}
for r in raw["data"]["results"]:
    sym = r["symbol"]
    bars = []
    for b in r["bars"]:
        if b.get("interpolated"):
            continue
        t = datetime.fromisoformat(b["begins_at"].replace("Z", "+00:00")).astimezone(ET)
        if t.strftime("%Y-%m-%d") != day:
            continue
        m = t.hour * 60 + t.minute
        bars.append(dict(m=m, o=float(b["open_price"]), h=float(b["high_price"]), l=float(b["low_price"]),
                         c=float(b["close_price"]), v=float(b["volume"])))
    bars.sort(key=lambda x: x["m"])
    st = dict(bars=len(bars), last_bar=(f"{bars[-1]['m']//60}:{bars[-1]['m']%60:02d}" if bars else None))
    orb = [b for b in bars if 570 <= b["m"] <= 584]
    if len(orb) < 15:
        st["status"] = "OR_NOT_READY"; res[sym] = st; continue
    orh, orl = max(b["h"] for b in orb), min(b["l"] for b in orb)
    st.update(ORH=orh, ORL=orl)
    # VWAP after each completed 1-min bar
    pv = vv = 0.0
    for b in bars:
        pv += (b["h"] + b["l"] + b["c"]) / 3 * b["v"]; vv += b["v"]; b["vwap"] = pv / vv
    # completed 5-min bars from 1-min bars; EMA continues from warm-up state
    e9, e20 = ema0[sym]["ema9"], ema0[sym]["ema20"]
    fives = []
    bym = {b["m"]: b for b in bars}
    for s in range(570, 960, 5):
        grp = [bym.get(s + k) for k in range(5)]
        if any(g is None for g in grp):
            break
        c = grp[-1]["c"]; e9 += 2 / 10 * (c - e9); e20 += 2 / 21 * (c - e20)
        fives.append(dict(s=s, end=s + 5, o=grp[0]["o"], h=max(g["h"] for g in grp), l=min(g["l"] for g in grp),
                          c=c, vwap=grp[-1]["vwap"], e9=e9, e20=e20))
    st.update(vwap=round(bars[-1]["vwap"], 3), ema9=round(e9, 3), ema20=round(e20, 3), last_close=bars[-1]["c"])
    events, signals = [], []
    for side in ("CALL", "PUT"):
        for f in fives:
            if f["end"] < 590:  # candle must close at/after 09:50
                continue
            rng = f["h"] - f["l"]
            if rng <= 0:
                continue
            body = abs(f["c"] - f["o"]) / rng
            if side == "CALL":
                ok = f["c"] >= orh * 1.0003 and f["c"] > f["vwap"] and f["e9"] > f["e20"] and f["c"] > f["o"] and body >= 0.5
            else:
                ok = f["c"] <= orl * 0.9997 and f["c"] < f["vwap"] and f["e9"] < f["e20"] and f["c"] < f["o"] and body >= 0.5
            if not ok:
                continue
            events.append(f"{side} breakout 5m bar {f['s']//60}:{f['s']%60:02d}-{f['end']//60}:{f['end']%60:02d} close {f['c']}")
            nxt = [b for b in bars if f["end"] <= b["m"] < f["end"] + 10]  # next ten completed 1-min bars
            for b in nxt:
                lvl = orh if side == "CALL" else orl
                touch = b["l"] <= lvl * 1.0005 and b["h"] >= lvl * 0.9995
                close_ok = (b["c"] > orh and b["c"] > b["vwap"]) if side == "CALL" else (b["c"] < orl and b["c"] < b["vwap"])
                if touch and close_ok:
                    sig_t = b["m"] + 1  # known at bar close
                    if ENTRY_START <= sig_t <= ENTRY_END:
                        signals.append(dict(side=side, time=f"{sig_t//60}:{sig_t%60:02d}", underlying=b["c"]))
                    else:
                        events.append(f"{side} retest at {sig_t//60}:{sig_t%60:02d} outside entry window")
                    break
            else:
                if len(nxt) < 10:
                    events.append(f"{side} retest window open ({len(nxt)}/10 bars seen)")
                else:
                    events.append(f"{side} no retest within 10 bars")
            break  # only the first breakout per side
    st.update(events=events, signals=signals)
    res[sym] = st
print(json.dumps(res, indent=1))
