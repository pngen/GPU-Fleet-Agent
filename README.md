# GPU Fleet Agent

GPU Fleet Agent is a C++20 systems runtime that answers one question with
authoritative, bounded, explainable state: **what accelerator resources exist
right now, which ones are healthy, reachable, compatible, trustworthy, and
ready for work, what changed since the last observation, and which fleet state
is authoritative enough to drive execution decisions?**

It is not a scheduler, not a Prometheus exporter, not an inventory script, and
not a generic monitoring daemon. GPU Fleet Agent owns node-local and
fleet-facing accelerator **state acquisition, normalization, health
interpretation, capability advertisement, liveness, freshness, authority,
fencing, recovery, and execution-eligibility reporting**. Other runtimes consume
its output for placement, admission, scheduling, failover, replication, model
residency, topology decisions, quota enforcement, resource brokering, latency
governance, and distributed recovery.

## Core principle

**A GPU is not AVAILABLE merely because the operating system can enumerate it.**
Execution eligibility depends on current, authoritative evidence about identity,
process incarnation, device presence, driver/runtime compatibility,
architecture, memory state, health, error state, reachability, freshness,
capability, maintenance/drain state, and generation authority.

## Architectural boundary

GPU Fleet Agent owns the question *what does this accelerator worker actually
have and what can it safely do now*. It deliberately does **not** own workload
placement, admission decisions, scheduling, failover, or replication. It reports
drain state; it does not become the workload scheduler. It never fabricates
workload ownership that belongs to another runtime.

## Identity model

All identity and authority values are strong typed C++ types, never raw
integers or loose strings. Separate types exist for `FleetId`, `NodeId`,
`WorkerId`, `WorkerBootId`, `DeviceId`, `DeviceUuid`, `DevicePciId`,
`DeviceGeneration`, `ObservationId`, `CapabilityId`, `HealthReportId`,
`LeaseId`, `RegistrationId`, `CoordinatorId`, and `AgentId`.

Generations roll **independently** per authority domain and are never collapsed
into one generic counter: `CoordinatorEpoch`, `FleetGeneration`,
`NodeGeneration`, `WorkerGeneration`, `DeviceGeneration`,
`ObservationGeneration`, `CapabilityGeneration`, `HealthGeneration`,
`RegistrationGeneration`, and `PolicyGeneration` are distinct types.

## Registration and authority

An explicit worker registration carries the node, worker, worker boot id, agent
version, protocol version, OS/platform, driver version, CUDA runtime version,
enumerated devices, supported capabilities, registration generation, and the
coordinator epoch. A restarted agent is given a **fresh `WorkerBootId`**; any
message from a prior process incarnation is fenced at the coordinator. The
coordinator rejects stale epochs, stale boot ids, stale registration
generations, duplicate registration identities, and impossible transitions.

## Device discovery and probing

Device stable identity is separated from ephemeral process identity. A device
is identified by vendor, UUID (when the platform exposes it), full PCI
domain/bus/device/function, architecture, compute capability, total physical
memory, NUMA node, driver and runtime identity, and MIG/parent relationships.
PCI position alone is **never** treated as globally stable identity.

The NVIDIA/CUDA backend gathers only information actually exposed by the CUDA
Runtime and CUDA Driver APIs. NVML is optional and is **not** required: the
runtime continues to function with CUDA alone, and temperature/power are
reported only when the platform surfaces them (otherwise they are *unknown*).
A backend abstraction allows non-NVIDIA accelerator providers to be added. A
synthetic backend is provided for deterministic, clearly-labeled synthetic
fleet scenarios.

## Capability model

Capabilities are explicit and never fabricated. Each carries a
`CapabilityKind` distinguishing **discovered** (probed), **validated** (a real
kernel/round-trip executed), **inferred** (derived from hardware identity), and
**unknown**. Vendor, architecture, compute capability, total and usable memory,
CUDA availability, runtime, supported dtypes, tensor cores, peer access,
unified addressing, graph support, cooperative launch, driver, and NUMA are
advertised where observably true.

## Health model

Health is a typed state: `UNKNOWN`, `HEALTHY`, `DEGRADED`, `UNHEALTHY`,
`DRAINING`, `DRAINED`, `OFFLINE`, `LOST`, `RECOVERING`, `QUARANTINED`. It is
derived from enumeration, driver/runtime availability, CUDA initialization,
memory allocation, kernel execution, synchronization, memory round-trip, fatal
error state, temperature/power limits, repeated operation failures, stale
observations, and administrative drain/quarantine. **A single successful
enumeration never yields HEALTHY.**

## Execution eligibility

Eligibility is independent from raw health and is always explainable. Typed
states: `ELIGIBLE`, `DEGRADED_ELIGIBLE`, `INELIGIBLE`, `DRAINING`, `STALE`,
`QUARANTINED`, `UNKNOWN`. Every decision carries the exact reasons - stale
observation, architecture mismatch, insufficient memory, driver mismatch, CUDA
unavailable, health failure, drain policy, generation mismatch, worker restart,
device disappearance, quarantine, capability mismatch.

## Freshness and the observation pipeline

Every dynamic observation carries a timestamp, an `ObservationGeneration`, the
source `WorkerBootId`, the `CoordinatorEpoch`, and a freshness threshold. Old
observations never silently remain current. The pipeline is conceptually
**enumerate - identify - probe - validate - normalize - compare - publish -
commit**. Publication is atomic from the perspective of readers; partial probe
results never silently replace previously authoritative complete state.

## Drain and quarantine

A device/node may transition ACTIVE - DRAINING - DRAINED. Draining rejects new
execution eligibility while preserving current authoritative state; the runtime
reports drain state and does not become the workload scheduler. Quarantine is
independent from drain and may be triggered by repeated validation failure,
fatal accelerator error, identity inconsistency, corrupted state, impossible
capability transition, or explicit operator action. Quarantined devices are
ineligible until explicitly and validly cleared, and quarantine records carry
reason, source, generation, timestamp, affected device, and authority.

## Distributed architecture

A real coordinator plus worker-agent OS processes talk over **framed TCP** with
explicit fencing using the coordinator epoch, worker boot id, and every
separate generation. The multiprocess proof starts a coordinator and two worker
OS processes, publishes observations, kills a worker as a real OS process, marks
its observations lost, rolls the coordinator epoch, restarts the worker with a
fresh boot id, replays stale messages on every authority axis and proves they
are rejected, verifies the other worker is unaffected, persists the coordinator
state, recovers, proves recovered dynamic observations are not treated as
fresh, refreshes both agents, and verifies a deterministic stable state digest.

Network behavior handles clean disconnect, abrupt process death, half-written
frames, reconnect, duplicate reconnect, stale replay, coordinator restart,
worker restart, delayed messages, and duplicate messages. An old connection can
never regain authority after a newer process incarnation is active.

## Persistence and recovery

State is persisted as a versioned binary store with a fixed byte order,
bounded lengths, an explicit schema version, and CRC-32 integrity. Writes are
atomic. Loads reject corruption, truncation, trailing garbage, duplicate ids,
invalid enums, invalid generations, and impossible state transitions. Recovery
reproduces a stable persistent-state digest. After restart, recovered dynamic
observations are **not** fresh: they become STALE until re-observed, and stale
workers never regain authority.

## RTX 5090 validation

On the real NVIDIA GeForce RTX 5090 (compute capability 12.0 / sm_120, 32.6 GB)
the runtime performs more than cudaGetDeviceCount(): it enumerates, resolves
stable identity, initializes CUDA, queries architecture and memory, records the
baseline free/used memory, allocates a bounded buffer, copies a known input
H2D, executes a real CUDA kernel, synchronizes, copies D2H, verifies against a
CPU reference, frees, re-queries memory, and confirms return to baseline. On
1,048,576 elements the validation reports **mem delta 0 bytes** and passes all
8 checks.

## CLI

The `gpufleet` CLI supports `list-nodes`, `list-workers`, `list-devices`,
`inspect-device`, `inspect-worker`, `health`, `capabilities`, `eligibility`,
`changes`, `snapshot`, `save`, `recover`, `run-coordinator`, `run-agent`,
`drain`, `undrain`, `quarantine`, `clear-quarantine`, and `info`. `--json` is
supported where useful.

## Build, install, use

Toolchain: C++20, CMake, MSVC on Windows, CUDA 13.1 where applicable.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CUDA is optional. When the CUDA toolkit is present, pass
`-DCMAKE_CUDA_COMPILER=<nvcc>` and the backend is built for sm_120; otherwise
the runtime degrades gracefully (the CUDA backend reports `cuda_available()`
false and `make_cuda_backend()` returns nullptr).

Install and consume:

```
cmake --install build --config Release --prefix _install
```

```cmake
find_package(GPUFleetAgent CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE GPUFleetAgent::gpufleet)
```

The namespace target is `GPUFleetAgent::gpufleet`. An independent downstream
consumer built purely against the installed package (no source access) links,
builds, and runs successfully.

## Examples

Runnable examples cover local device discovery, capability inspection,
execution eligibility, hardware validation (RTX 5090), health transition, stale
observation, drain/undrain, quarantine/recovery, worker restart, coordinator
epoch rollover, stale-authority rejection, persistence/recovery, and JSON
snapshot.

## Benchmark summary

Meaningful completed-op throughput (Release, RTX 5090 host):

| Operation | Ops/s |
| --- | --- |
| registration | ~20.8 M/s |
| device snapshot ingestion | ~1.0 M/s |
| health evaluation | ~41.9 M/s |
| eligibility evaluation | ~448 M/s |
| capability normalization | ~3.3 M/s |
| indexed device lookup (1000 devices) | ~21.7 M/s |
| snapshot serialization | ~714 K/s |
| recovery (restore) | ~2.9 M/s |
| state-diff generation | ~7.1 M/s |
| protocol encode+decode | ~776 K/s |
| concurrent observation ingestion (8 threads) | ~365 K/s |

All workloads are counted as completed operations and units are per-operation.

## Testing

The deterministic test suite covers strong IDs, generation types, device
identity, registration, WorkerBootId restart semantics, device enumeration
abstraction, health model, capability model, execution eligibility, freshness,
stale observations, device changes, drain, quarantine, recovery, persistence,
corruption, truncation, deterministic serialization, protocol framing,
malformed protocol input, stale CoordinatorEpoch, stale WorkerBootId, stale
registration/observation/health/device generations, reconnect behavior, abrupt
process death, coordinator restart, concurrency, exact accounting, the real
RTX 5090 CUDA validation, and the downstream install/package consumer.
Property/randomized tests use fixed seeds printed to the console.

## Limitations

- Temperature and power are surfaced only when the platform (NVML) is present;
  otherwise they are reported as unknown rather than assumed.
- A single GPU was available for validation; physical multi-GPU and multi-node
  validation is not claimed. Large-fleet and multi-device scenarios use the
  clearly-labeled synthetic backend.
- Stable identity is anchored on vendor + PCI + architecture + compute
  capability + physical memory + driver version; the device UUID is used when
  exposed by the platform (the CUDA runtime API used for this build did not
  surface it, so UUID was left empty and documented, never silently reused).
- The runtime reports state; it is not a scheduler and does not make placement
  decisions.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.