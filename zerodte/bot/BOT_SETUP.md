# 0DTE bot setup — iron condor + credit spreads on SPY

The bot is a set of **scheduled Claude routines** that run in the cloud and trade through the
**Robinhood MCP connector**. No server or broker API keys are needed.

| Connector | Used for |
|---|---|
| Robinhood | quotes, option chains, orders, positions |
| FMP (optional) | economic-calendar check |
| Gmail (optional) | daily report to yourself |

Python cannot call these connectors from a script, so the routines *are* the bot.

The rules below are exactly what was backtested (`../REPORT.md`). Anything marked
**untested** was not in the backtest.

---

## Step 0 — Account prerequisites (blocking)

Status checked on 2026-11-08 for your agent-tradable account **"MCP" (••••8852)**:

| Requirement | Now | Needed |
|---|---|---|
| Options level | Level 2 | **Level 3** (spreads/condors). Apply: https://applink.robinhood.com/upgrade_options?account_number=537538852&source=mcp |
| Account type | limited margin | margin or limited margin ✅ (Level 3 is not allowed on cash accounts) |
| Cash | $84.29 | **≥ $300 recommended for $1-wide** spreads, **≥ $600 for $2-wide**. One full loss is about $90 per $1 of width. The backtest's $100 account busted in 1 of 4 years. |
| Trade approvals | **OFF** (orders execute immediately) | **Turn ON** in the Robinhood app for the dry-run and the first live weeks. Every order then waits for your tap. |
| Day-trade rule | — | Closing a 0DTE spread the same day it opened is a day trade. Margin accounts under $25k are limited to 3 day trades per 5 business days under the PDT rule. Check your account's day-trade status in the app first. If you're capped, see "PDT mode" below. |

## Step 1 — Strategy rules (what the bot does)

All three run on **SPY, today's expiry (0DTE)**. Use **one structure per day**: the condor
already contains a put spread and a call spread, so running all three would triple the risk.

### A. Iron condor — core strategy
- **Entry 11:00 ET.**
- **Short legs:** the call and the put whose |delta| is closest to **0.30**.
- **Wings:** **$2** further out-of-the-money. Use **$1** if the account is under $600.
- **Order:** 4-leg limit **credit** at the natural mid.
  - Not filled in 3 minutes: lower the credit $0.01 and retry, up to 3 times.
  - Still not filled: skip the day.
- **Stop:** close when the cost to close reaches **2× the credit received** (a loss equal to the credit).
- **Time exit:** close everything at **15:50 ET**.
- **Backtest:** OOS win rate 65%; +$156/trade at $100k sizing; +$9.50/trade at 1 lot with $1 wings.

### B. Put credit spread — alternative to A
- 0.30-delta short put, $2 wide.
- Entry 11:00, no stop, close 15:50.
- OOS win rate 82%.

### C. ORB signal → credit spread — advanced, phase 3
Your VWAP + ORB rules, but **selling** a spread in the signal's direction instead of buying an option.

**Entry signal (5-minute bars):**
- **Opening range:** high/low of 09:30–09:45.
- **Breakout:** a candle closes beyond the range with volume above its 20-bar average.
- **Retest:** within 6 bars, a candle's low (or high, for puts) touches the range edge and
  closes back outside it.
- **Trend:** close above VWAP and EMA9 > EMA21 for bullish; the opposite for bearish.
- **Window:** the signal must happen 09:45–11:30.

**Trade:**
- **Bullish signal:** sell a 0.40-delta put spread, $2 wide.
- **Bearish signal:** sell a 0.40-delta call spread, $2 wide.
- **Exits:** no stop; close at 15:50.
- Max 2 per day.

**Backtest:** OOS win rate 73%; +$81/trade.

**Scheduling:** this needs a check every 5 minutes, but cloud routines run at most hourly.
Run C only from a live session using `/loop 5m` between 09:44 and 11:31 (see the prompt
file), or skip it.

### Risk rules (all strategies)
- **Size:** 1 contract until the account exceeds $2,000. After that, keep the max loss per
  trade at or below **2% of the account**.
- **Daily limit:** at most 1 condor/spread per day (C: 2). Stop for the day after any losing close.
- **Weekly kill switch:** if the account is down 3 max-losses from its peak, the bot stops
  and emails you.
- **Never leave a short leg open after 15:55.** SPY options are physically settled, so an
  in-the-money short leg means assigned shares.
- **Optional, untested:** skip FOMC/CPI/NFP days using the FMP economic calendar.

## Step 2 — Dry run first (2–3 weeks, no orders)

The backtest uses **modelled** option prices. The dry run checks them against real fills.
Create the routines with `MODE=DRY_RUN`. Each run:
- logs the strikes, live bid/ask, and the credit it would have taken;
- records the result at 15:50;
- emails you the comparison.

Go live only if real credits are within ~15% of the model and the paper P&L is not clearly
negative.

## Step 3 — Create the routines

In a Claude session, ask:

> Create my 0DTE routines from `zerodte/bot/routine_prompts.md` in DRY_RUN mode.

Claude will create three routines.

| Routine | Schedule (America/New_York, Mon–Fri) | Job |
|---|---|---|
| `0dte-entry` | 11:00 | check prerequisites and event day; pick strikes; place (or log) the condor |
| `0dte-monitor` | hourly 12:00–15:00 | check the stop (2× credit); close if hit |
| `0dte-close` | 15:50 | close any open 0DTE legs; write the daily log; email the report |

Each routine needs the Robinhood connector, plus Gmail and FMP if you want the email and
the event filter.

Note that the stop is only checked hourly (the routine minimum). The backtest checked every
5 minutes, so expect slightly larger losses on stop days.

## Step 4 — Go live
1. Get Level 3 approved and deposit capital. Keep trade approvals **ON**.
2. Ask Claude to switch the routines to `MODE=LIVE`.
3. After about 20 live trades, compare to the backtest:
   - win rate around 65–75%;
   - average loss around the credit size.
   If the numbers are clearly worse, stop.
4. Only then consider turning approvals off.

## PDT mode (if you are day-trade-restricted)
- Skip the 15:50 close and let the spread **expire**. That is not a day trade. The backtest
  showed holding to expiry performs about the same as closing at 15:50.
- **But:** if SPY closes between your short and long strike, you will be assigned shares.
  Robinhood may close the position for you near the close.
- Use this mode only with $1-wide spreads, and only if you understand the assignment risk.
