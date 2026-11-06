#!/usr/bin/env bash
# Engage the kill switch: blocks new entries, forces RESEARCH mode, revokes order authorizations.
# Then cancel pending opening orders via Robinhood MCP (see .claude/skills/kill-switch).
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/agent.py kill-switch on --reason "${1:-emergency stop}"
