# $100 0DTE bot — SPY iron condor ($1 wings), step by step

**One strategy only.** It is the only setup that held up for a $100 account in the backtest
(`../REPORT.md`, `../results/final100.csv`):

> **SPY 0DTE iron condor**: short call and short put at **0.30 delta**, long wings **$1** further
> out-of-the-money. Opened at **14:30 ET**, closed at **15:50 ET**, **no stop**, 1 contract.

The bot trades **every weekday**. FINRA replaced the pattern-day-trader rule (the $25k minimum
and the 3-day-trades-per-5-days limit) with intraday margin standards, effective
**June 4, 2026** (Regulatory Notice 26-10). Brokers may phase this in until Oct 2027.

**Fallback:** if Robinhood still shows a day-trade counter or limit on your account, use the
Mon/Wed/Fri schedule in the table below until it doesn't.

## What to expect (994-day backtest, 1 contract, modelled option prices)

| plan | trades | win % | avg/trade | worst | avg week | $100 → end of year (Y1 / Y2 / Y3 / Y4) |
|---|---|---|---|---|---|---|
| **every weekday, close 15:50 (this bot)** | 994 | 75% | **+$9.53** | −$89 | **+$45.3** | $2,003 / **busted** / $2,922 / $2,864 |
| Mon/Wed/Fri only, close 15:50 (fallback if your broker still limits day trades) | 589 | 75% | +$9.81 | −$78 | +$27.7 | $1,113 / busted / $1,927 / $2,059 |
| every day, hold to expiry (no day trade) | 994 | 71% | +$10.74 | −$89 | +$51.1 | not recommended: SPY assignment risk |

Years: Y1 = Oct 2022–Sep 2023, Y2 = Oct 2023–Sep 2024, Y3 = Oct 2024–Sep 2025,
Y4 = Oct 2025–Sep 2026.

- **Collateral per condor:** about **$61** (median). Robinhood holds $1 width × 100 − credit.
- **Typical credit:** about $0.35–0.45 per condor, i.e. **+$35–45 when it wins**.
- **Busted in 1 of 4 years:** two max losses early in Oct 2023 took the account below the
  collateral needed for one trade.
- **Option prices are modelled.** Do the dry run below before risking money.

## Prerequisites
1. **Options Level 3 approved** (applied). Claude re-checks this before every trade.
2. **Account "MCP" (••••8852)** funded with **≥ $100**. $300+ gives a buffer of several
   losses and would have avoided the busted year.
3. **Trade approvals ON** in the Robinhood app: Account → Investing → (agent) trade approvals.
   Each order then waits for your tap until you trust the bot.
4. **No day-trade limit on the account.** Check the Robinhood app for a day-trade counter. If
   one is still shown, use the Mon/Wed/Fri fallback schedule.

## The rules the bot follows (exactly what was backtested)
1. **Days:** every weekday (Mon–Fri). Skip market holidays and half-days.
   - If the condor could not be closed on a given day, the bot does not trade the next day.
2. **14:30 ET — pick strikes** on today's SPY expiry:
   - short call: delta closest to +0.30; long call: short call strike + $1;
   - short put: delta closest to −0.30; long put: short put strike − $1.
3. **Order:** 4-leg limit **credit**, at the mid credit rounded down to $0.01, good-for-day.
   - Not filled in 3 minutes: retry $0.01 lower, up to 3 times.
   - Still not filled: no trade today.
   - Skip if the credit is under $0.15 or the collateral exceeds buying power.
4. **15:50 ET — close** all four legs with a limit debit at the natural price.
   - Retry $0.02 higher every minute, with the last try by 15:57.
   - **Never hold past the close.** An in-the-money short leg means assigned SPY shares.
5. **Size:** 1 contract. Above **$600**, contracts = floor(account ÷ 300), which keeps ~3 max
   losses of buffer per contract. This scaling was **not** in the backtest.
6. **Stop trading and email you** if any of these happen:
   - the account can't cover one condor's collateral;
   - **3 losing trades in a row**;
   - the account is down **50% from its peak**.
   You restart it manually.
7. **Optional, untested:** skip FOMC / CPI / jobs-report days using the FMP economic calendar.

## Setup steps
1. **Dry run, 2–3 weeks (about 10–15 trading days).** Ask Claude:
   *"Create my $100 0DTE condor routines from zerodte/bot/routine_prompts_100.md in DRY_RUN mode."*
   - Claude creates 2 routines: entry at 14:30 and close at 15:50, Mon–Fri, New York time.
   - Each needs the Robinhood connector (plus Gmail for the report, FMP for the event filter).
   - Each trading day you get an email showing the real credit vs the model's and the P&L it
     would have made.
2. **Go/no-go.**
   - Go live if real credits come within ~15% of the model's and total paper P&L is not
     clearly negative.
   - Otherwise stop. The modelled prices were wrong for live trading.
3. **Go live.** Ask Claude: *"Switch my 0DTE routines to LIVE."* Keep approvals ON and approve
   each order in the app.
4. **Review after 20 live trades.**
   - Expect a win rate around 70–75% and losses averaging about $40–60.
   - If clearly worse, stop the routines. Ask Claude to disable them (they are kept, not deleted).
