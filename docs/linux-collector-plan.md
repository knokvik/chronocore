# Linux collector plan

ChronoCore’s claim is only meaningful if the kernel collector is honest about CPU support and privilege. The portable build uses a synthetic feed. The Linux build has a `perf_event_open` mmap-ring collector for the generic cache-miss event; it does **not** claim PEBS on macOS, AMD, or every Intel Linux CPU.

## Implemented path

1. The target application writes 64-byte measured-latency markers into a POSIX shared-memory SPSC ring.
2. The daemon drains that ring and a task-scoped `perf_event_open` mmap ring. Its current collector requests `precise_ip=2`, preserves timestamp/PID/TID/CPU/IP, and correlates a sample against the marker’s measured execution span (not merely its completion timestamp).
3. If the PMU or permissions reject the request, startup fails with an actionable error. It never silently switches to a less precise profiler.

The current collector uses `PERF_COUNT_HW_CACHE_MISSES`, which is deliberately labelled cache-miss attribution—not L3 attribution. Use PMU-specific event discovery before adding a CPU-specific L3 claim.

## Production path

1. Probe PMU capabilities and choose raw L3/branch events only where the CPU documents them.
2. Add grouped per-CPU perf events and account for multiplexing, lost samples, and context switches.
3. Add an optional CO-RE libbpf marker path for environments where a shared-memory transport is not suitable. Avoid syscall-wide probes in the hot path.
4. Resolve instruction pointers from build IDs and process mappings.
5. Measure overhead with a pinned workload, `perf stat`, isolated CPUs, and controlled event periods. Publish the CPU model, kernel, sample period, workload, and confidence interval with every number.

## Important constraints

- PEBS is Intel-specific; AMD needs an IBS implementation with explicitly different precision claims.
- A counter sample is not automatically caused by the nearest marker. The correlator needs sequence/context checks and a calibrated timing-error budget before presenting causal language.
- “Zero overhead” and universal ±10 ns precision are not defensible product promises. Targets must be reported per CPU, kernel, event, and workload.
- Persisted baselines need versioning by binary build ID, CPU model, and collector configuration. Mixing them would create false regressions.
