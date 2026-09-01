#pragma once
// Deterministic execution-eligibility evaluation.
//
// Eligibility is pure and stateless given the input. It is evaluated
// independently of raw health: a HEALTHY device can still be INELIGIBLE for
// architectural, memory, capability, freshness, or generation reasons.
#include <cstdint>
#include <vector>

#include "gpufleetagent/eligibility/eligibility.hpp"
#include "gpufleetagent/health/health_state.hpp"
#include "gpufleetagent/types/generations.hpp"

namespace gpufleet {

/// All signals required to evaluate eligibility for one device scope.
struct EligibilityInput {
  HealthState health = HealthState::UNKNOWN;
  bool observation_fresh = false;       // current, within the freshness threshold
  DeviceGeneration observed_generation{};
  DeviceGeneration current_generation{};
  bool architecture_match = true;
  bool sufficient_memory = true;
  bool driver_match = true;
  bool cuda_available = false;
  bool capability_match = true;
  bool drain_active = false;
  bool quarantine = false;
  bool worker_alive = true;
  bool device_present = true;
  bool worker_restarted = false;        // a newer WorkerBootId superseded this one
  std::uint64_t required_memory = 0;
  std::uint64_t available_memory = 0;
};

/// Map a health state to its baseline eligibility state, without the extra
/// policy gates (memory/architecture/capability/freshness).
inline EligibilityState baseline_eligibility(HealthState h) {
  switch (h) {
    case HealthState::HEALTHY: return EligibilityState::ELIGIBLE;
    case HealthState::DEGRADED: return EligibilityState::DEGRADED_ELIGIBLE;
    case HealthState::DRAINING: return EligibilityState::DRAINING;
    case HealthState::DRAINED: return EligibilityState::DRAINING;
    case HealthState::QUARANTINED: return EligibilityState::QUARANTINED;
    case HealthState::OFFLINE:
    case HealthState::LOST: return EligibilityState::INELIGIBLE;
    case HealthState::RECOVERING: return EligibilityState::STALE;
    case HealthState::UNHEALTHY: return EligibilityState::INELIGIBLE;
    case HealthState::UNKNOWN:
    default: return EligibilityState::UNKNOWN;
  }
}

/// Deterministic eligibility evaluation. Pure; no time, no I/O.
inline EligibilityResult evaluate_eligibility(const EligibilityInput& in) {
  EligibilityResult r;

  auto add = [&r](EligibilityReason reason) { r.reasons.push_back(reason); };

  // Quarantine is an independent, dominating fence.
  if (in.quarantine) {
    r.state = EligibilityState::QUARANTINED;
    add(EligibilityReason::QUARANTINE);
    return r;
  }

  // Drain rejects new eligibility but preserves current authoritative state.
  if (in.drain_active) {
    r.state = EligibilityState::DRAINING;
    add(EligibilityReason::DRAIN_POLICY);
    return r;
  }

  // Staleness and worker-incarnation checks must gate BEFORE health so that a
  // healthy-but-stale observation never yields ELIGIBLE.
  if (in.worker_restarted || !in.worker_alive) {
    r.state = EligibilityState::STALE;
    add(EligibilityReason::WORKER_RESTART);
  } else if (in.observed_generation != in.current_generation) {
    r.state = EligibilityState::STALE;
    add(EligibilityReason::GENERATION_MISMATCH);
  } else if (!in.observation_fresh) {
    r.state = EligibilityState::STALE;
    add(EligibilityReason::STALE_OBSERVATION);
  }

  if (r.state == EligibilityState::STALE) {
    // A stale scope never returns ELIGIBLE. It may still accumulate additional
    // hard reasons, but STALE dominates unless quarantine/drain already fired.
    return r;
  }

  if (!in.device_present) {
    r.state = EligibilityState::INELIGIBLE;
    add(EligibilityReason::DEVICE_DISAPPEARANCE);
    return r;
  }
  if (!in.cuda_available) {
    r.state = EligibilityState::INELIGIBLE;
    add(EligibilityReason::CUDA_UNAVAILABLE);
  } else if (!in.driver_match) {
    r.state = EligibilityState::INELIGIBLE;
    add(EligibilityReason::DRIVER_MISMATCH);
  } else if (!in.architecture_match) {
    r.state = EligibilityState::INELIGIBLE;
    add(EligibilityReason::ARCHITECTURE_MISMATCH);
  } else if (!in.sufficient_memory || in.available_memory < in.required_memory) {
    r.state = EligibilityState::INELIGIBLE;
    add(EligibilityReason::INSUFFICIENT_MEMORY);
  } else if (!in.capability_match) {
    r.state = EligibilityState::INELIGIBLE;
    add(EligibilityReason::CAPABILITY_MISMATCH);
  }

  if (r.state == EligibilityState::INELIGIBLE) {
    return r;
  }

  // Fall through to health.
  switch (baseline_eligibility(in.health)) {
    case EligibilityState::ELIGIBLE:
      r.state = EligibilityState::ELIGIBLE;
      break;
    case EligibilityState::DEGRADED_ELIGIBLE:
      r.state = EligibilityState::DEGRADED_ELIGIBLE;
      add(EligibilityReason::HEALTH_FAILURE);
      break;
    case EligibilityState::STALE:
      r.state = EligibilityState::STALE;
      add(EligibilityReason::STALE_OBSERVATION);
      break;
    case EligibilityState::QUARANTINED:
      r.state = EligibilityState::QUARANTINED;
      add(EligibilityReason::QUARANTINE);
      break;
    case EligibilityState::DRAINING:
      r.state = EligibilityState::DRAINING;
      add(EligibilityReason::DRAIN_POLICY);
      break;
    case EligibilityState::INELIGIBLE:
    case EligibilityState::UNKNOWN:
    default:
      r.state = EligibilityState::INELIGIBLE;
      add(EligibilityReason::HEALTH_FAILURE);
      break;
  }
  return r;
}

}  // namespace gpufleet
