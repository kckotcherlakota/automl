#!/usr/bin/env bash
# Backtest every stored dataset (crypto from Coinbase, stocks ingested from Robinhood MCP).
set -euo pipefail
cd "$(dirname "$0")/.."
shopt -s nullglob
for f in data/crypto/*.json data/stocks/*_5m.json; do
  python3 scripts/agent.py backtest "$f" --include-research
done
