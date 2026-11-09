# $100 bot — routine prompts (paste-ready, self-contained)

**Current mode: DRY_RUN** (switched back from LIVE on 2026-11-08 before any live trade: a re-run backtest showed the
edge disappears with a 15:40 close or +1¢/leg slippage — see notes/long_term_notes.md).

Create two routines, each **starting a fresh session on every run**, scheduled in New York time,
Monday–Friday:

| routine | cron |
|---|---|
| `0DTE condor entry (DRY RUN)` | `CRON_TZ=America/New_York 30 14 * * 1-5` |
| `0DTE condor close (DRY RUN)` | `CRON_TZ=America/New_York 50 15 * * 1-5` |

- **Day-trade fallback:** if your broker still enforces a day-trade limit, use `1,3,5` instead of `1-5`.
- **Connectors:** the session that runs them needs the **Robinhood** and **Gmail** connectors.
  **FMP** is optional.
- **Email:** reports go to `kckota9@gmail.com` (already filled in both prompts).
- **Going live:** change `MODE=DRY_RUN` to `MODE=LIVE` only after the 2–3 week dry run (see
  README).

---

## condor-entry (14:30 ET, Mon–Fri)

```
MODE=DRY_RUN. You are my $100 SPY+QQQ 0DTE iron-condor bot, entry step (14:30 America/New_York).
If MODE=DRY_RUN you must NOT call review_option_order, place_option_order or any order/cancel tool:
read-only Robinhood calls and email only.

Account: the Robinhood account nicknamed "MCP" (the one get_accounts marks as tradable by you).
Never use any other account. Email every report to kckota9@gmail.com with the Gmail connector.

Pre-checks — on any failure, email "0DTE bot: skipped - <reason>" and stop:
 a. get_accounts: the MCP account must have option_level_3.
 b. Market open today, not a half-day; SPY has an expiration dated today (get_option_chains SPY).
 c. Search Gmail for the latest "0DTE daily report"; stop if it contains "KILL SWITCH" (LIVE only:
    also stop if it says the previous close did not complete).
 d. Optional: if the FMP economics calendar shows an FOMC decision, CPI or nonfarm payrolls today,
    LIVE -> skip the day; DRY_RUN -> still price it but flag "event day".

For SYMBOL in [SPY, QQQ] — QQQ only if remaining buying power (get_portfolio buying power minus
the SPY collateral) still covers QQQ's collateral:
 1. get_equity_quotes SYMBOL -> spot. get_option_instruments chain_symbol=SYMBOL,
    expiration_dates=today, strikes within ±2% of spot; get_option_quotes in batches of <= 20.
 2. Short call = call with delta closest to +0.30; long call = short call strike + 1.
    Short put = put with delta closest to -0.30; long put = short put strike - 1.
 3. mid credit = mid(sc) + mid(sp) - mid(lc) - mid(lp);
    natural credit = bid(sc) + bid(sp) - ask(lc) - ask(lp); collateral = (1 - mid credit) * 100.
 4. SKIP if mid credit < 0.15 or collateral > remaining buying power.
 5. Quantity = 1 (if account value > 600: floor(account value / 300)).
 6. LIVE only: review_option_order with the 4 legs (sell sc open, buy lc open, sell sp open,
    buy lp open), direction=credit, type=limit, price = mid credit rounded DOWN to 0.01,
    time_in_force=gfd; report any order_checks; then place_option_order with the same parameters
    and a fresh ref_id. If unfilled after 3 minutes, cancel and retry 0.01 lower, max 3 retries;
    then cancel and report "no fill". If rejected for a day-trading reason, email
    "switch to Mon/Wed/Fri" and stop.

Email subject "0DTE entry <DRY|LIVE> <YYYY-MM-DD>". Body: an exact JSON block plus a short summary
with, per symbol: time, spot, 4 strikes with each leg's bid/ask/mid/delta, mid and natural credit,
(LIVE: filled credit), collateral, quantity, SKIP reason, buying power, event-day flag.
The 15:50 close routine reads this JSON.
```

## condor-close (15:50 ET, Mon–Fri)

```
MODE=DRY_RUN. $100 SPY+QQQ 0DTE condor end-of-day close (15:50 America/New_York).
If MODE=DRY_RUN you must NOT call review_option_order, place_option_order or any order/cancel tool.
Account: Robinhood "MCP" only. Email reports to kckota9@gmail.com with the Gmail connector.

LIVE: find open SPY and QQQ option positions expiring today (get_option_positions nonzero=true).
For each symbol close ALL legs in one 4-leg limit DEBIT order (buy to close shorts, sell to close
longs, position_effect=close) at the natural debit = ask(shorts) - bid(longs). Review, then place
with a fresh ref_id. If unfilled after 1 minute, cancel and retry +0.02; repeat until 15:57; at
15:57 use natural debit + 0.05. Never leave a short leg open after this routine.
DRY_RUN: read today's "0DTE entry DRY" email JSON from Gmail; for each non-skipped symbol get
current quotes of the 4 legs and compute the natural closing debit.

P&L per symbol = (entry credit - closing debit) * 100 * quantity - 0.05 * 8 * quantity
(DRY_RUN: report it both with the mid credit and with the natural credit).

Email subject "0DTE daily report <YYYY-MM-DD>":
 - per symbol: entry credit, closing debit, P&L, win/loss
 - running totals from all earlier "0DTE daily report" emails: trades, win rate, total P&L,
   peak and drawdown (start equity $204.29, dry run from 2026-11-09)
 - put "KILL SWITCH - bot stopped" at the TOP if: 3 losing days in a row, or equity < 50% of peak,
   or equity < $70 (can't fund one condor).
```

---

# Notes system (memory between days)

All routines run inside one persistent Claude session (the org does not allow connectors on
fresh-session routines), so the chat history grows and is compacted automatically. The bot
does **not** rely on it: every run treats earlier chat as stale and works only from fresh
Robinhood/Gmail data plus two files in this repo, on branch `claude/busy-davinci-8zond5`:

| file | written | cleared |
|---|---|---|
| `notes/daily_notes.md` | morning prep, entry, close (each appends its section for today) | every Friday by the weekly review |
| `notes/long_term_notes.md` | weekly review (summary, running totals, lessons) | never; kept short |

Four routines, all Mon–Fri New York time unless noted:

| routine | cron |
|---|---|
| `0DTE morning prep (DRY RUN)` | `CRON_TZ=America/New_York 7 9 * * 1-5` |
| `0DTE condor entry (DRY RUN)` | `CRON_TZ=America/New_York 30 14 * * 1-5` |
| `0DTE snapshot 15:35 (DRY RUN)` | `CRON_TZ=America/New_York 35 15 * * 1-5` |
| `0DTE snapshot 15:40 (DRY RUN)` | `CRON_TZ=America/New_York 40 15 * * 1-5` |
| `0DTE condor close (DRY RUN)` | `CRON_TZ=America/New_York 50 15 * * 1-5` |
| `0DTE weekly review` | `CRON_TZ=America/New_York 17 16 * * 5` (Fridays) |
| `0DTE daily session clear` (prompt: `/clear`) — **disabled**: a routine-sent `/clear` does not clear the session | `CRON_TZ=America/New_York 13 17 * * 1-5` |

The entry and close routines run the prompts above **followed by** the notes protocol below.

## Notes protocol (appended to the entry and close prompts)

```
NOTES PROTOCOL (memory between days; do this as well as the steps above):
- Earlier messages in this conversation are stale. Do not reuse any number, quote, strike or
  conclusion from them. Your memory between runs is ONLY the two notes files plus fresh data.
- START, before anything else: cd /home/user/automl (if missing: git clone
  https://github.com/kckotcherlakota/automl /home/user/automl). Then
  git fetch origin claude/busy-davinci-8zond5 && git checkout claude/busy-davinci-8zond5 &&
  git pull --ff-only origin claude/busy-davinci-8zond5.
  Read notes/long_term_notes.md and notes/daily_notes.md in full. Follow today's morning-prep
  plan if there is one; if it is missing, do the morning-prep checks yourself first.
- END, after the email is sent: append this step's section under today's "## YYYY-MM-DD (Day)"
  heading in notes/daily_notes.md (create the heading if missing), following the format at the
  top of that file. Entry step: "### Entry (14:30 ET)". Close step: "### Close (15:50 ET)" and
  then "### Learnings" (real vs model credit, anything unexpected, tool/data problems; mark
  untested ideas as untested). Keep each section under ~15 lines.
  Then: git add notes && git commit -m "notes: <YYYY-MM-DD> <entry|close>" with the trailer
  lines "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" and
  "Claude-Session: https://claude.ai/code/session_012ZPnNbEARMTpMMQ1Weeq33";
  git push -u origin claude/busy-davinci-8zond5 (on a network error retry up to 4 times,
  waiting 2s, 4s, 8s, 16s). If the push still fails, say so in the email.
- Never write account numbers in full, secrets or tokens into the notes.
```

## 0DTE morning prep (09:07 ET, Mon–Fri)

```
MODE=DRY_RUN. $100 SPY+QQQ 0DTE condor bot, MORNING PREP (09:07 America/New_York). Read-only:
do NOT call review_option_order, place_option_order or any order/cancel tool.
Account: Robinhood "MCP" only. Email to kckota9@gmail.com with the Gmail connector ONLY if
something needs my attention (see below); otherwise no email.

Earlier messages in this conversation are stale; do not reuse anything from them.
1. cd /home/user/automl (if missing: git clone https://github.com/kckotcherlakota/automl
   /home/user/automl); git fetch origin claude/busy-davinci-8zond5 && git checkout
   claude/busy-davinci-8zond5 && git pull --ff-only origin claude/busy-davinci-8zond5.
   Read notes/long_term_notes.md and notes/daily_notes.md in full.
2. Checks (read-only):
   a. get_accounts: MCP still has option_level_3.
   b. Market open today and not a half-day; SPY expiration dated today (get_option_chains SPY);
      same for QQQ.
   c. get_portfolio: buying power and account value. Can it fund SPY (~$61) and QQQ (~$70)?
   d. get_option_positions nonzero=true: any open option positions (there should be none).
   e. Gmail: latest "0DTE daily report" - KILL SWITCH present? Did yesterday's close run
      (a daily report exists for the last trading day)?
   f. Optional: FMP economics calendar - FOMC decision, CPI or nonfarm payrolls today?
   g. get_trade_approval_setting for MCP (record on/off; must be ON before LIVE).
3. Append "## YYYY-MM-DD (Day)" and "### Morning prep (~09:00 ET)" to notes/daily_notes.md:
   the results above plus a short plan for 14:30 (which symbols to price, event-day flag,
   anything from long-term or daily notes to watch). Keep it under ~12 lines.
4. Commit "notes: <YYYY-MM-DD> morning prep" with trailer lines
   "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" and
   "Claude-Session: https://claude.ai/code/session_012ZPnNbEARMTpMMQ1Weeq33", then
   git push -u origin claude/busy-davinci-8zond5 (retry up to 4 times: 2s, 4s, 8s, 16s).
5. Email "0DTE morning prep: ATTENTION <YYYY-MM-DD>" only if: market closed/half-day,
   KILL SWITCH, yesterday's close missing, open positions found, buying power < $70, options
   level changed, or the notes push failed. Never write full account numbers or secrets.
```

## 0DTE weekly review (16:17 ET, Fridays)

```
$100 SPY+QQQ 0DTE condor bot, WEEKLY REVIEW (Friday 16:17 America/New_York). Read-only on
Robinhood: do NOT call review_option_order, place_option_order or any order/cancel tool.
Earlier messages in this conversation are stale; do not reuse anything from them.
1. cd /home/user/automl (clone https://github.com/kckotcherlakota/automl there if missing);
   git fetch origin claude/busy-davinci-8zond5 && git checkout claude/busy-davinci-8zond5 &&
   git pull --ff-only origin claude/busy-davinci-8zond5. Read both notes files in full.
   Cross-check numbers against this week's "0DTE daily report" emails in Gmail.
2. Update notes/long_term_notes.md:
   - add one row to "Running totals" for this week (trades, wins, win %, week P&L mid and
     natural, cumulative natural P&L, peak, drawdown);
   - update "Real vs model credit" with this week's average real mid/natural credit vs the
     model's $0.35-0.45 and the % gap;
   - add at most 5 new bullets to "Lessons" (newest first); merge or delete lessons that are
     now duplicated or proven wrong; keep the whole file under ~150 lines;
   - update "Open questions"; after 10-15 dry-run trading days, add a go/no-go line using the
     README rule (credits within ~15% of model, paper P&L not clearly negative).
3. Reset notes/daily_notes.md to just its header and format block (everything above the
   first "---" line plus that line), deleting the week's day entries.
4. Commit "notes: weekly review week ending <YYYY-MM-DD>" with trailer lines
   "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" and
   "Claude-Session: https://claude.ai/code/session_012ZPnNbEARMTpMMQ1Weeq33"; git push -u origin
   claude/busy-davinci-8zond5 (retry up to 4 times: 2s, 4s, 8s, 16s).
5. Email kckota9@gmail.com (Gmail connector) "0DTE weekly review <YYYY-MM-DD>": the week's row,
   cumulative totals, real-vs-model credit, new lessons, and go/no-go status if due.
```

## 0DTE snapshot 15:35 / 15:40 (DRY RUN, Mon–Fri)

Two routines with the same prompt except the time (routines must be ≥ 1 hour apart, so one
routine cannot run at both 15:35 and 15:40). Read-only, no email: they read today's entry JSON
from Gmail, quote the 4 legs, compute natural/mid closing debit and P&L, and append
"### Snapshot HH:MM ET" to `notes/daily_notes.md`. The 15:50 close report compares all three
close times; the weekly review keeps running totals per close time. Purpose: test whether the
edge survives a close before Robinhood's 15:45 auto-close of at-risk late-close options.
