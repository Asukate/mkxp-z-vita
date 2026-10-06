#!/usr/bin/env bash
# The same source checks, clean bootstrap, compile, and packaging run in CI and locally.
set -euo pipefail
PROFILE="${1:-real-vita}"
WS="${2:-/work}"
JOBS="${JOBS:-4}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
git config --global --add safe.directory "$ROOT"
mkdir -p "$WS/logs"
python3 ci/check-source.py 2>&1 | tee "$WS/logs/source.log"
python3 tests/vita-release.py 2>&1 | tee "$WS/logs/release-boundary.log"
python3 tests/ruby-standby.py 2>&1 | tee "$WS/logs/ruby-standby.log"
python3 tests/runtime-log.py 2>&1 | tee "$WS/logs/runtime-log.log"
python3 tests/archive-resume.py 2>&1 | tee "$WS/logs/archive-resume.log"
python3 tests/vitagl-finish.py 2>&1 | tee "$WS/logs/vitagl-finish.log"
./scripts/bootstrap-vita.sh --ws "$WS" --profile "$PROFILE" -j "$JOBS" 2>&1 | tee "$WS/logs/bootstrap.log"
./scripts/build-vita.sh --ws "$WS" --profile "$PROFILE" -j "$JOBS" 2>&1 | tee "$WS/logs/build.log"
./scripts/package-launcher.sh --ws "$WS" --profile "$PROFILE" 2>&1 | tee "$WS/logs/package.log"
cp "$WS"/*state.json "$WS/bootstrap-manifest.txt" "$WS/packages/"
(cd "$WS/packages" && sha256sum *.vpk *state.json bootstrap-manifest.txt > SHA256SUMS)
