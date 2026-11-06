"""Integration tests: C++ tool CLIs, the PreToolUse order gate, and mode/kill-switch
safety. Uses mocks only - never contacts Robinhood and never places an order."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build"
HOOK = REPO / ".claude" / "hooks" / "pre_order_gate.py"
ACCT = "537538852"


def run_tool(name, payload):
    r = subprocess.run([str(BUILD / name), "--input", "-"], input=json.dumps(payload), capture_output=True, text=True)
    return r.returncode, json.loads(r.stdout)


def flat_bars(n=300, start=1_700_000_000, tf=900, px=100.0):
    return [{"t": start + i * tf, "o": px, "h": px * 1.001, "l": px * 0.999, "c": px, "v": 10} for i in range(n)]


class Sandbox(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        shutil.copytree(REPO / "config", self.root / "config")
        (self.root / "state").mkdir()
        os.symlink(BUILD, self.root / "build")
        self.env = {**os.environ, "TRADING_AGENT_ROOT": str(self.root)}
        self.session = "sess-A"

    def tearDown(self):
        shutil.rmtree(self.root)

    def write(self, rel, obj):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(obj))

    def gate(self, tool, args, session=None):
        payload = {"session_id": session or self.session, "tool_name": f"mcp__Robbinhood__{tool}", "tool_input": args}
        r = subprocess.run(["python3", str(HOOK)], input=json.dumps(payload), capture_output=True, text=True, env=self.env)
        self.assertEqual(r.returncode, 0)
        if not r.stdout.strip():
            return "allow", ""
        out = json.loads(r.stdout)["hookSpecificOutput"]
        return out["permissionDecision"], out["permissionDecisionReason"]

    def live(self, session="sess-A", ttl=3600):
        self.write("state/mode.json", {"mode": "LIVE", "session_id": session, "expires_at": time.time() + ttl})

    def auth(self, name="a1", **over):
        rec = {"tool": "place_crypto_order", "session_id": self.session, "symbol": "BTC-USD", "side": "buy",
               "type": "limit", "max_quantity": 0.0001, "max_limit_price": 75000.0, "expires_at": time.time() + 120}
        rec.update(over)
        self.write(f"state/authorizations/{name}.json", rec)

    def crypto_buy(self, **over):
        a = {"rhs_account_number": ACCT, "symbol": "BTC-USD", "side": "buy", "type": "limit",
             "quantity": "0.0001", "limit_price": "74990.00"}
        a.update(over)
        return a


class OrderGateTests(Sandbox):
    def test_research_mode_denies(self):
        self.auth()
        d, why = self.gate("place_crypto_order", self.crypto_buy())
        self.assertEqual(d, "deny")
        self.assertIn("RESEARCH", why)

    def test_options_and_advanced_always_denied(self):
        self.live()
        for t in ("place_option_order", "place_advanced_order", "exercise_option"):
            self.assertEqual(self.gate(t, {"account_number": ACCT})[0], "deny")

    def test_matching_authorization_is_single_use(self):
        self.live(); self.auth()
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy())[0], "allow")
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy())[0], "deny")  # duplicate order blocked

    def test_modified_intent_denied(self):
        self.live(); self.auth()
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(quantity="0.0002"))[0], "deny")
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(limit_price="75100"))[0], "deny")
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(symbol="ETH-USD"))[0], "deny")
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(type="market"))[0], "deny")
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(dollar_amount="5", quantity=None))[0], "deny")

    def test_expired_authorization_and_activation(self):
        self.live(); self.auth(expires_at=time.time() - 1)
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy())[0], "deny")
        self.live(ttl=-1); self.auth("a2")
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy())[0], "deny")

    def test_other_session_cannot_use_live(self):
        self.live(session="sess-B"); self.auth()
        d, why = self.gate("place_crypto_order", self.crypto_buy())
        self.assertEqual(d, "deny")
        self.assertIn("different", why)

    def test_account_mismatch_denied(self):
        self.live(); self.auth()
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(rhs_account_number="641767827"))[0], "deny")

    def test_kill_switch_blocks_buys_but_allows_exits(self):
        self.live(); self.auth()
        self.write("state/kill_switch.json", {"engaged": True})
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy())[0], "deny")
        self.write("state/mode.json", {"mode": "RESEARCH"})
        sell = self.crypto_buy(side="sell", type="stop_loss", stop_price="70000", limit_price=None)
        self.assertEqual(self.gate("place_crypto_order", sell)[0], "allow")

    def test_sell_outside_live_or_emergency_denied(self):
        self.assertEqual(self.gate("place_crypto_order", self.crypto_buy(side="sell"))[0], "deny")

    def test_garbage_payload_denied(self):
        r = subprocess.run(["python3", str(HOOK)], input="not json", capture_output=True, text=True, env=self.env)
        self.assertIn("deny", r.stdout)


class ToolCliTests(unittest.TestCase):
    def test_stale_data_rejected(self):
        bars = flat_bars()
        now = bars[-1]["t"] + 900 + 10_000
        rc, out = run_tool("signal_engine", {"asset_class": "crypto", "timeframe": "15m", "now": now, "bars": bars})
        self.assertEqual(rc, 3)
        self.assertTrue(out["stale"])

    def test_incomplete_bar_dropped(self):
        bars = flat_bars()
        now = bars[-1]["t"] + 100  # last bar still open
        rc, out = run_tool("signal_engine", {"asset_class": "crypto", "timeframe": "15m", "now": now, "bars": bars})
        self.assertEqual(out["incomplete_bars_dropped"], 1)

    def test_invalid_input_exit_code(self):
        rc, out = run_tool("signal_engine", {"asset_class": "crypto", "timeframe": "15m", "now": 1,
                                             "bars": [{"t": 1, "o": 1, "h": 0.5, "l": 1, "c": 1, "v": 1}]})
        self.assertEqual(rc, 2)
        self.assertEqual(out["error_type"], "invalid_input")

    def test_risk_engine_paper_vs_research(self):
        acct = {"equity": 10000, "buying_power": 10000, "now": 1, "operating_mode": "RESEARCH"}
        prop = {"symbol": "BTC-USD", "asset_class": "crypto", "entry": 75000, "stop": 74000, "qty_increment": 1e-8}
        rc, out = run_tool("risk_engine", {"account": acct, "proposal": prop})
        self.assertEqual(rc, 3)
        acct["operating_mode"] = "PAPER"
        rc, out = run_tool("risk_engine", {"account": acct, "proposal": prop})
        self.assertEqual(rc, 0, out)
        self.assertLessEqual(out["risk_dollars"], 25.0 + 1e-9)

    def test_drawdown_manager_requires_explicit_unlock(self):
        acct = {"equity": 10000, "buying_power": 10000, "now": 1, "persisted_mode": "LOCKED"}
        _, out = run_tool("drawdown_manager", {"account": acct})
        self.assertEqual(out["new_persisted_mode"], "LOCKED")
        _, out = run_tool("drawdown_manager", {"account": acct, "operator_unlock": {"authorized": True, "by": "op", "reason": "review done"}})
        self.assertEqual(out["new_persisted_mode"], "NORMAL")
        acct["equity"] = 9000; acct["high_water_mark"] = 10000  # 10% dd: unlock cannot go below metrics
        _, out = run_tool("drawdown_manager", {"account": acct, "operator_unlock": {"authorized": True, "by": "op", "reason": "x"}})
        self.assertEqual(out["new_persisted_mode"], "LOCKED")

    def test_order_optimizer_quote_moved(self):
        rc, out = run_tool("order_optimizer", {"quote": {"bid": 101, "ask": 101.01, "ts": 100}, "signal_price": 100,
                                               "tick_size": 0.01, "now": 101})
        self.assertEqual(rc, 3)


class AgentCliTests(Sandbox):
    def agent(self, *args):
        return subprocess.run(["python3", str(REPO / "scripts" / "agent.py"), *args], capture_output=True, text=True, env=self.env)

    def test_live_mode_refused_without_readiness(self):
        r = self.agent("set-mode", "LIVE", "--confirm-live", "--operator", "x", "--session-id", "s")
        self.assertEqual(r.returncode, 3)
        self.assertIn("readiness checks failed", r.stdout)

    def test_kill_switch_forces_research_and_revokes(self):
        self.live(); self.auth()
        r = self.agent("kill-switch", "on", "--reason", "test")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(json.loads((self.root / "state/mode.json").read_text())["mode"], "RESEARCH")
        self.assertFalse(list((self.root / "state/authorizations").glob("*.json")))

    def test_authorize_refused_outside_live(self):
        self.write("req.json", {})
        r = self.agent("authorize-order", str(self.root / "req.json"))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("LIVE required", r.stderr)


if __name__ == "__main__":
    unittest.main()
