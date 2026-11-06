#!/usr/bin/env bash
# Print risk state (paper account by default, or a live snapshot JSON path as $1).
set -euo pipefail
cd "$(dirname "$0")/.."
if [ $# -ge 1 ]; then python3 scripts/agent.py risk-check --live-snapshot "$1"; else python3 scripts/agent.py risk-check; fi
