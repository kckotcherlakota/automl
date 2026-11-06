#!/usr/bin/env bash
# One paper cycle (simulated fills, public market data). Safe to schedule every 15 min; overlapping runs are skipped.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/agent.py paper-cycle
