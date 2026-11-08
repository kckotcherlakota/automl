# 0DTE SPY/QQQ backtest — VWAP + ORB, iron condor, credit spread, calendar

**Data:** 994 full trading days of real 1-minute SPY and QQQ bars, 2022-10-03 → 2026-09-28
(aggregated to 5-minute bars), from free Hugging Face datasets (see `fetch_data.py`).
Implied vol comes from Cboe's daily VIX1D / VIX9D / VIX / VXN files.

**Important limitation: option prices are modelled, not historical quotes.** No free source has
1000 days of 0DTE option quotes. Every option is priced with Black-Scholes, using the day's
opening implied vol held constant through the day, plus a bid/ask model
(SPY ≈ $0.01–0.02 half-spread, QQQ ≈ $0.015–0.03). As a sanity check, the IV inputs average
1.29× (SPY) and 1.18× (QQQ) the realized intraday vol, in line with the published
variance-risk premium. The model leaves out:
- volatility skew,
- intraday IV moves, including the 0DTE IV crush into the close,
- early assignment and pin risk,
- the fact that SPY/QQQ options are physically settled.

Treat the absolute numbers as approximate. The *relative* conclusions are much more robust.

Data caveat: Apr–Sep 2026 (126 days) comes from an IEX-only feed with ~23% narrower 5-minute
ranges. That slightly flatters premium-selling in "Y4". The SIP-only years show the same
direction.

## 1. Your proposed rules (VWAP + ORB + EMA9/21, retest, 0.50–0.65Δ, −20% / +40%)

| | trades | win % | avg $/trade | PF | return | max DD |
|---|---|---|---|---|---|---|
| SPY + QQQ ($100k, 0.5% risk) | 640 | 28% | **−$65** | 0.80 | −41.8% | −45.9% |
| SPY only | 292 | 26% | −$78 | 0.75 | −22.8% | −24.2% |
| QQQ only | 396 | 28% | −$72 | 0.78 | −28.6% | −33.5% |
| same entry times, **random direction** (10 seeds) | ~648 | ~34% | −$37 to −$87 (mean −$61) | | | |

- **The signal is no better than a coin flip** at picking direction.
- Removing any single filter doesn't help:
  - retest, volume, VWAP, EMA, and the technical exit were each removed in turn;
  - all variants stay negative.
- Exit/contract grid of 144 cells:
  - only 2 are positive;
  - the best in-sample cell (+$25/trade) scores −$0.6/trade out of sample.
- With a 20% stop, a 40% target and a ~2.5-hour hold, the cost structure needs a win rate
  above ~33%. The rules deliver 28%.

## 2. All strategy families (3,973 configs, C++ engine, 23 s)

Walk-forward test: for each family, pick the best config by average $/trade using only the
first 60% of days (to 2025-02-27), then score it on the remaining 40%.

| family | cells | cells positive | picked in-sample | IS $/trade | OOS $/trade | OOS win % |
|---|---|---|---|---|---|---|
| **iron condor** | 714 | 300 | 30Δ / $2 wings / 11:00 / 1× stop, SPY | +149 | **+156** | 65% |
| put credit spread | 684 | 212 | 30Δ / $2 / 11:00 / no stop, SPY | +65 | +72 | 82% |
| call credit spread | 684 | 234 | 30Δ / $2 / 11:00 / 2× stop, SPY | +56 | +80 | 79% |
| ORB signal → credit spread | 72 | 17 | 40Δ / $2 / hold, SPY+QQQ | +88 | +81 | 73% |
| trend-directed credit spread | 678 | 210 | 30Δ / $2 / 11:00, SPY | +54 | +56 | 77% |
| ORB long option (your rules + grid) | 144 | 2 | — | +25 | **−0.6** | 39% |
| **calendar** (0DTE short / 1DTE long) | 864 | **0** | — | −9 | −22 | 56% |

- **≥80% win rate:** 818 cells reach it.
  - 476 of those are profitable over the full period.
  - 356 are profitable both in-sample and out-of-sample.
- **Best ≥80% cell:** SPY iron condor, 20Δ shorts / $2 wings, 11:00 entry, held to close.
  - 82% wins, +$112 per trade on a $100k account (1% risk).
  - Average win +$259, average loss −$542, max DD −3.2%.

**What actually works here is selling premium.** Long-option breakouts and calendars don't.

## 3. $100 account (1 contract, $0.05/contract fees, forced close 15:50)

The strategy was picked without look-ahead from spreads a $100 account can afford
(≤ $1-wide spreads). Each year was simulated separately, starting from $100.

| strategy | win % | avg/trade (1 lot) | worst trade | Y1 22-23 | Y2 23-24 | Y3 24-25 | Y4 25-26* |
|---|---|---|---|---|---|---|---|
| **SPY iron condor 30Δ, $1 wings, 14:30** | 75% | +$9.5 | −$89 | **$2,003** | **busted Oct-23** | **$2,922** | **$2,864** |
| SPY put credit spread 30Δ, $1, 11:00 | 80% | +$5.3 | −$106 | $1,831 | busted | busted | busted |
| ORB → credit spread 30Δ, $1 | 79% | +$4.0 | −$138 | $700 | busted | busted | busted |
| QQQ trend credit spread 30Δ, $1 | 77% | +$3.3 | −$105 | $969 | $841 | $428 | busted |
| your ORB rules (premium ≤ $1) | 26% | −$3.1 | −$23 | busted | busted | busted | $63 |
| calendar | 26% | −$38.5 | −$231 | $49 | $61 | $83 | $100 (rarely affordable) |

Average week for the best case (condor, 1 lot, **if the account survives**): **+$35 to +$52**,
which works out to roughly **$1,900–2,800 net in a year**.

- **Variance is the main risk.** A single max loss (~$90) on a $100 account ends it.
  - The best strategy still busted in 1 of 4 years: two losses in the first weeks of Oct 2023.
  - Starting with ~$300–500 (3–5 max losses of buffer) would have avoided every bust here.
- **$1,000/week from $100 is not supported by anything tested.**
  - The best 1-lot week in 4 years was about $234.
  - Even at 10 contracts (~$1,000 of capital) the average is ~$350–500/week.
  - At that size a bad week is −$1,000 or worse.

## Files

| file | contents |
|---|---|
| `cpp/fastsim.cpp` | C++17 multi-threaded engine for all families. Matches the Python reference to <0.5% (`engine.py`, `condor.py`). |
| `fast.py` | `prep` builds the 5-minute bars with IV; `run` executes the full grid, walk-forward and $100 sim. |
| `final100.py` | $100-account yearly simulation of the picked configs. |
| `run_backtest.py`, `engine.py`, `condor.py` | Python reference implementation. |
| `results/fast_summary.csv` | One row per config. |
| `results/fast_walk_forward.csv`, `results/final100.csv` | Tables above. |

Rebuild:

```bash
python fetch_data.py
g++ -O3 -std=c++17 -pthread -o cpp/fastsim cpp/fastsim.cpp
python fast.py prep
python fast.py run
python final100.py
```

*Not investment advice. All results are from modelled option prices.*
