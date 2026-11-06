#!/usr/bin/env bash
# Local side of reconciliation; broker positions/orders come from Robinhood MCP and are authoritative.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/agent.py positions
