#pragma once
// Deterministic health evaluation.
//
// Health evaluation is pure and stateless: given a set of observed signals it
// deterministically yields a typed HealthState and a human-readable
// explanation. It never consults wall-clock time directly; staleness is an
// input signal so that tests may drive it.
#include <cstdint>
#include <string>

#include "gpufleetagent/health/health_state.hpp"

namespace gpufleet {

/// Observed, per-device health signals. Fields are defaulted to the most
/// conservative value (false / "not OK") except temperature and power, which
/// default to "within limits" so that a probe that does not surface them does
/// not falsely degrade a device. Where a signal is not observable the caller
/// records it explicitly as unobserved; the evaluator treats a missing core
/// operational check as NOT proven, which can only ever move the result DOWN
/// from HEALTHY, never UP.
struct HealthSignals {
  bool enumerated = false;             // the device could be enumerated
  bool device_present = true;          // the device is present right now
  bool was_known = false;              // a previous authoritative observation existed
  bool driver_runtime_ok = false;
  bool cuda_init_ok = false;
  bool memory_alloc_ok = false;
  bool kernel_exec_ok = false;
  bool sync_ok = false;
  bool mem_roundtrip_ok = false;
  bool fatal_error = false;            // sticky fatal accelerator error
  bool temperature_within_limits = true;
  bool power_within_limits = true;
  std::uint32_t consecutive_failures = 0;
  bool observation_stale = false;
  bool explicitly_drained = false;     // admin drain requested
  bool inflight_work_remaining = false; // reported by an external runtime, may be false when unknown
  bool quarantined = false;            // quarantine is independent of drain
  bool last_validation_ok = false;
  std::uint32_t failure_threshold = 3; // consecutive failures before UNHEALTHY
};

/// A structured, explainable health result.
struct HealthResult {
  HealthState state = HealthState::UNKNOWN;
  std::string explanation;

  bool operator==(const HealthResult& o) const {
    return state == o.state && explanation == o.explanation;
  }
};

/// Deterministic health evaluation. Pure; no time, no I/O.
inline HealthResult evaluate_health(const HealthSignals& s) {
  // Quarantine is an independent axis that dominates every other state:
  // a quarantined device is never merely "degraded"; it is ineligible until
  // explicitly and validly cleared.
  if (s.quarantined) {
    return {HealthState::QUARANTINED,
            "quarantined: administratively or automatically fenced; ineligible until validly cleared"};
  }

  // Administrative drain. Preserve current authoritative evidence, reject new
  // execution eligibility; the runtime only reports drain state.
  if (s.explicitly_drained) {
    if (s.inflight_work_remaining) {
      return {HealthState::DRAINING,
              "draining: rejecting new eligibility, in-flight work still reported present"};
    }
    return {HealthState::DRAINED, "drained: rejecting new eligibility and awaiting drain completion"};
  }

  // Device presence is the first hard gate after quarantine/drain.
  if (!s.device_present) {
    if (!s.was_known) {
      return {HealthState::OFFLINE, "offline: device not enumerable and never previously observed"};
    }
    return {HealthState::LOST, "lost: device was present but has disappeared and is not enumerable"};
  }

  // Fatal errors and driver/runtime availability failures are UNHEALTHY.
  if (s.fatal_error) {
    return {HealthState::UNHEALTHY, "unhealthy: fatal accelerator error recorded"};
  }
  if (!s.driver_runtime_ok) {
    return {HealthState::UNHEALTHY, "unhealthy: driver or runtime availability failed"};
  }
  if (!s.cuda_init_ok) {
    return {HealthState::UNHEALTHY, "unhealthy: CUDA initialization failed"};
  }
  if (s.consecutive_failures >= s.failure_threshold) {
    return {HealthState::UNHEALTHY,
            "unhealthy: repeated operation failures (" + std::to_string(s.consecutive_failures) +
                " consecutive)"};
  }

  // A stale observation means we have no current, trustworthy evidence. The
  // device is treated as RECOVERING (uncertain until refreshed), which is
  // never allowed to satisfy current eligibility unless policy says otherwise.
  if (s.observation_stale) {
    return {HealthState::RECOVERING,
            "recovering: last evidence is stale and is not fresh enough to be authoritative"};
  }

  // The operational core: all of alloc/kernel/sync/round-trip must be proven.
  const bool core_ok =
      s.memory_alloc_ok && s.kernel_exec_ok && s.sync_ok && s.mem_roundtrip_ok;

  if (!core_ok) {
    if (s.last_validation_ok) {
      return {HealthState::RECOVERING,
              "recovering: core validation not currently proven; last validation was OK"};
    }
    return {HealthState::UNHEALTHY, "unhealthy: core device validation not proven"};
  }

  if (!s.temperature_within_limits || !s.power_within_limits) {
    return {HealthState::DEGRADED,
            "degraded: core validation passed but temperature or power is outside limits"};
  }

  return {HealthState::HEALTHY, "healthy: enumeration, driver/runtime, and core validation all proven"};
}

}  // namespace gpufleet
