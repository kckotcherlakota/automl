#!/usr/bin/env bash
# Install build dependencies (Debian/Ubuntu) and build. No trading happens here.
set -euo pipefail
cd "$(dirname "$0")/.."
if command -v apt-get >/dev/null; then
  SUDO=$([ "$(id -u)" = 0 ] && echo "" || echo sudo)
  $SUDO apt-get update -q && $SUDO apt-get install -y -q g++ cmake nlohmann-json3-dev libgtest-dev libbenchmark-dev libsqlite3-dev python3
fi
mkdir -p state data reports
[ -f state/mode.json ] || echo '{"mode": "RESEARCH"}' > state/mode.json
scripts/build.sh
