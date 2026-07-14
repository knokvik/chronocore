#!/usr/bin/env bash
# End-to-end markers-only validation for hosts without a CPU PMU (e.g. Lightning).
# Usage: from repo root, ./scripts/cloud-markers-validation.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
RESULTS="${CHRONOCORE_RESULTS:-$ROOT/../chronocore-results}"
mkdir -p "$RESULTS"
SHM="${CHRONOCORE_SHM:-/chronocore-validation}"
PORT="${CHRONOCORE_PORT:-18080}"
REGRESSION_MS="${CHRONOCORE_REGRESSION_MS:-5000}"

pkill -9 -f chronocore-daemon 2>/dev/null || true
pkill -9 -f chronocore-example 2>/dev/null || true
sleep 1
# POSIX shm object lives under /dev/shm on Linux.
rm -f "/dev/shm${SHM}" 2>/dev/null || true

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

./build/chronocore-example-engine "$SHM" "$REGRESSION_MS" \
  >"$RESULTS/engine.log" 2>&1 &
ENGINE_PID=$!
echo "ENGINE_PID=$ENGINE_PID"
sleep 1
ls -la "/dev/shm${SHM}"

./build/chronocore-daemon \
  --shm "$SHM" \
  --target-pid "$ENGINE_PID" \
  --port "$PORT" \
  --baseline "$RESULTS/validation.baseline" \
  --baseline-key "markers-validation|$(uname -m)" \
  >"$RESULTS/daemon.log" 2>&1 &
DAEMON_PID=$!
echo "DAEMON_PID=$DAEMON_PID"
trap 'kill "$DAEMON_PID" "$ENGINE_PID" 2>/dev/null || true' EXIT

sleep 2
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
  echo "daemon failed:" >&2
  cat "$RESULTS/daemon.log" >&2
  exit 1
fi
cat "$RESULTS/daemon.log" || true

# Warm-up + regression window + headroom for baseline + 3σ.
sleep $((REGRESSION_MS / 1000 + 12))

curl -fsS "http://127.0.0.1:${PORT}/api/health" | tee "$RESULTS/health.json"
echo
curl -fsS "http://127.0.0.1:${PORT}/api/metrics" | tee "$RESULTS/metrics.json"
echo

python3 - "$RESULTS/metrics.json" "$RESULTS/daemon.log" <<'PY'
import json
import sys
from pathlib import Path

metrics = json.loads(Path(sys.argv[1]).read_text())
log = Path(sys.argv[2]).read_text()
funcs = metrics.get("functions") or []
alerts = metrics.get("alerts") or []
assert funcs, "expected at least one function in metrics"
ob = next((f for f in funcs if f.get("function") == "OrderBook::insert"), None)
assert ob is not None, "missing OrderBook::insert"
assert int(ob.get("samples") or 0) >= 100, f"too few samples: {ob}"
print(f"OK samples={ob['samples']} mean={ob['mean_latency_ns']} alerts={len(alerts)}")
if "hardware attribution unavailable" in log:
    print("OK PMU-fallback warning present (expected without CPU PMU)")
if alerts:
    print(f"OK regression alerts fired: {len(alerts)}")
else:
    print("WARN: no alerts yet (may need longer run); samples path still OK")
PY

echo "validation artifacts in $RESULTS"
