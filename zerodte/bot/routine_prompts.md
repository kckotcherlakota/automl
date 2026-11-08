# Routine prompts (paste-ready)

Every prompt starts from a fresh session, so each one is fully self-contained.
Replace `MODE=DRY_RUN` with `MODE=LIVE` only after the dry run (see `BOT_SETUP.md`).
Account: the Robinhood account nicknamed "MCP" (the one Claude is allowed to trade).

---

## 0dte-entry  — weekdays 11:00 America/New_York

```
MODE=DRY_RUN. You are my SPY 0DTE iron-condor bot. Use the Robinhood connector.
1. get_accounts -> use the agent-tradable account. Stop (and email me) if option_level is not
   option_level_3, or get_portfolio buying power < max loss of the trade below.
   Call get_trade_approval_setting and include it in the log.
2. If today is not a trading day or SPY has no expiration today (get_option_chains SPY), stop.
3. Optional event filter: FMP economics calendar; if FOMC rate decision, CPI or NFP is today, log
   "event day - skip" and stop.
4. If a 0DTE SPY option position is already open (get_option_positions, expiration today), stop.
5. get_equity_quotes SPY. Load today's SPY instruments (get_option_instruments, expiration today)
   for strikes within ±3% of spot, get_option_quotes for them (batches of <=20).
   Short call = call with delta closest to +0.30; short put = put with delta closest to -0.30.
   Wings: long call = short call strike + W, long put = short put strike - W, where
   W = 2 if buying power >= 600 else 1.
6. Credit = (bid short call + bid short put) - (ask long call + ask long put); mid credit uses mids.
   Max loss per contract = (W - credit) * 100. Quantity = 1 unless account > $2000, then
   floor(0.02 * account / max loss).
7. If MODE=DRY_RUN: do not order. Append a JSON line {date, spot, legs, bid/ask per leg, natural
   credit, mid credit, max_loss} to the log (email it to me with subject "0DTE entry DRY").
   If MODE=LIVE: review_option_order (4 legs, direction=credit, limit = mid credit rounded down to
   0.01, gfd). Surface any order_checks. Then place_option_order with a fresh ref_id. If unfilled
   after 3 minutes, cancel and retry 0.01 lower, max 3 times; then give up for the day.
8. Email me: strikes, credit, max loss, stop level (= 2 x credit cost-to-close), order status.
```

## 0dte-monitor — weekdays 12:00, 13:00, 14:00, 15:00 America/New_York

```
MODE=DRY_RUN. SPY 0DTE condor stop check. Robinhood connector.
Find today's 0DTE SPY condor (LIVE: get_option_positions expiration today; DRY_RUN: today's
logged legs from the entry email). Get quotes for the 4 legs.
Cost to close = ask(short call) + ask(short put) - bid(long call) - bid(long put).
If cost to close >= 2 x entry credit: LIVE -> review then place a 4-leg limit DEBIT close order
at the natural cost to close (position_effect close), retry +0.02 up to 3 times;
DRY_RUN -> log "would stop at X". Email me only if a stop triggered.
```

## 0dte-close — weekdays 15:50 America/New_York

```
MODE=DRY_RUN. SPY 0DTE end-of-day close. Robinhood connector.
LIVE: if any SPY option expiring today is open, close all legs with a limit debit order at the
natural price (retry +0.02 up to 3 times, last try by 15:57). Never leave a short leg open.
Both modes: compute today's P&L (credit - closing cost - fees), append to the log, and email me
"0DTE daily report" with: entry credit vs model, exit, P&L, running total, win rate, and peak-to-
now drawdown. If drawdown >= 3 x average max loss, write "KILL SWITCH - stop trading" at the top.
```

## (Phase 3, optional) ORB credit-spread loop — run in a live session

```
/loop 5m  Between 09:45 and 11:30 ET: get today's SPY 5-minute bars (get_equity_historicals,
interval 5minute, today). Opening range = 09:30-09:45 high/low. Compute VWAP, EMA9, EMA21,
20-bar average volume. Signal rules: see zerodte/bot/BOT_SETUP.md section C. On the first new
bullish signal sell a 0.40-delta SPY 0DTE put spread $2 wide (bearish: call spread); max 2 per
day; skip if the condor routine already has a position. After 11:31 stop the loop.
```
