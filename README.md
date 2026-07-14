# ChronoCore

> Sub-microsecond, hardware-aware performance regression detection for C++ systems.

ChronoCore correlates application-measured latency events with hardware-counter observations, maintains constant-memory online statistics, and surfaces a live 3σ latency regression alert. The portable core and application marker transport are testable on any POSIX host; the hardware collector is Linux-only.

## What works today

- Hardware samples correlate against the full application-measured operation span, with a bounded 1 ms delivery-retention window and ±500 ns boundary tolerance.
- Per-function Welford mean/variance and constant-memory P² p99 estimator.
- Live 3σ regression alerts after a 100-sample baseline; three consecutive
  excursions open one incident, which avoids alerting on an isolated outlier.
- Local dashboard, JSON API, and SSE stream; a deterministic synthetic source demonstrates an `OrderBook::insert` regression.
- POSIX shared-memory marker ring for a real C++ application, plus a Linux `perf_event_open` collector for cache-miss samples.
- No runtime dependencies beyond the C++ standard library.

## Run the demo

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/chronocore-daemon --port 8080 --demo-regression-after-ms 5000
```

Open <http://127.0.0.1:8080>. After roughly five seconds, the synthetic source starts adding a 180 ns delay and cache misses to `OrderBook::insert`; the dashboard should issue a 3σ alert shortly after.

## Collector boundary

On production Linux, the collector and marker transport feed these two inputs:

```cpp
engine.ingest_event({sequence, timestamp_ns, "OrderBook::insert", measured_latency_ns, pid, tid, cpu});
engine.ingest_counter({timestamp_ns, cache_miss_period, 0, pid, tid, cpu, instruction_pointer});
```

This keeps privileged, kernel- and CPU-specific code out of the correlation and detection core. See [docs/linux-collector-plan.md](docs/linux-collector-plan.md) for the implemented collector path, roadmap, and caveats.

## API

- `GET /api/health` — process health.
- `GET /api/metrics` — function metrics and recent alerts.
- `GET /api/stream` — SSE metric and alert updates.

The dashboard uses Server-Sent Events with a fetch fallback.

## Linux application + perf collection

On a Linux host, start the example target and then attach ChronoCore to its POSIX shared-memory markers and perf cache-miss stream:

```bash
./build/chronocore-example-engine /chronocore-example
# In another terminal, replace PID with the example engine PID.
sudo ./build/chronocore-daemon --shm /chronocore-example --target-pid PID --port 8080 \
  --baseline /var/lib/chronocore/example.baseline --baseline-key 'build-id|cpu-model|cache-misses-period-10000'
```

`--baseline-key` is mandatory with persistence. Make it change whenever the target binary, CPU model, kernel collector configuration, or event period changes; mismatched records are ignored.

The generic collector reports `PERF_COUNT_HW_CACHE_MISSES` as cache-miss attribution. Do not label it “L3 misses” on a CPU until its PMU event semantics have been verified.

If the cloud VM does not expose a CPU PMU, ChronoCore logs a warning and continues in markers-only mode, retaining all latency metrics and regression alerts. Add `--require-perf` to fail startup instead—useful for a hardware-attribution benchmark that must not silently fall back.

Shared-memory rings are created mode `0666` (via `fchmod` after `shm_open`) so a non-root instrumented process and a `sudo` daemon can share the same ring without a manual `chmod`.
