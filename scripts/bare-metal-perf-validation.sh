#!/usr/bin/env bash
# End-to-end PMU validation. Run on a trusted Linux host with a usable CPU PMU.
# Example: CHRONOCORE_SUDO=1 ./scripts/bare-metal-perf-validation.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
RESULTS="${CHRONOCORE_RESULTS:-$ROOT/../chronocore-perf-results}"
SHM="${CHRONOCORE_SHM:-/chronocore-perf-validation}"
PORT="${CHRONOCORE_PORT:-18081}"
REGRESSION_MS="${CHRONOCORE_REGRESSION_MS:-10000}"
SUDO=()
if [[ "${CHRONOCORE_SUDO:-0}" == "1" ]]; then SUDO=(sudo); fi

mkdir -p "$RESULTS"
rm -f "$RESULTS/validation.baseline" "/dev/shm${SHM}" 2>/dev/null || true

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

./build/chronocore-example-engine "$SHM" "$REGRESSION_MS" >"$RESULTS/engine.log" 2>&1 &
ENGINE_PID=$!
sleep 1

"${SUDO[@]}" ./build/chronocore-daemon \
  --shm "$SHM" \
  --target-pid "$ENGINE_PID" \
  --require-perf \
  --port "$PORT" \
  --baseline "$RESULTS/validation.baseline" \
  --baseline-key "perf-validation|$(uname -m)|$(uname -r)" \
  >"$RESULTS/daemon.log" 2>&1 &
DAEMON_PID=$!
trap 'kill "$DAEMON_PID" "$ENGINE_PID" 2>/dev/null || true' EXIT

sleep 2
if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
  echo "perf daemon failed:" >&2
  cat "$RESULTS/daemon.log" >&2
  exit 1
fi

# Warm up the baseline, then run through the injected cache-pressure regression.
sleep $((REGRESSION_MS / 1000 + 12))
curl -fsS "http://127.0.0.1:${PORT}/api/health" | tee "$RESULTS/health.json"
echo
curl -fsS "http://127.0.0.1:${PORT}/api/metrics" | tee "$RESULTS/metrics.json"
echo

python3 - "$RESULTS/metrics.json" <<'PY'
import json
import sys
from pathlib import Path

metrics = json.loads(Path(sys.argv[1]).read_text())
function = next((x for x in metrics.get("functions", []) if x.get("function") == "OrderBook::insert"), None)
assert function is not None, "missing OrderBook::insert"
assert function["samples"] >= 100, f"too few samples: {function['samples']}"
assert function["cache_misses_per_event"] > 0, "no hardware sample was attributed to an operation"
assert metrics.get("alerts"), "expected an injected-regression alert"
print("OK samples={samples} cache_misses_per_event={cache_misses_per_event} alerts={alerts}".format(
    alerts=len(metrics["alerts"]), **function))
PY

echo "validated PMU attribution; artifacts in $RESULTS"
