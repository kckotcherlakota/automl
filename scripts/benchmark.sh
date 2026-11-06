#!/usr/bin/env bash
# Local computation latency only; says nothing about broker/exchange latency.
set -euo pipefail
cd "$(dirname "$0")/.."
./build/benchmark --benchmark_min_time=0.2s "$@"
