#pragma once
// Execution eligibility model.
//
// Eligibility is independent from raw health. A device may report HEALTHY
// health and still be INELIGIBLE for a particular scope (architecture
// mismatch, insufficient memory, capability mismatch, stale observation,
// generation mismatch). Eligibility results are always explainable: they
// carry the exact set of reasons that produced the decision.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/health/health_state.hpp"

namespace gpufleet {

enum class EligibilityState : std::uint8_t {
  UNKNOWN = 0,
  ELIGIBLE = 1,
  DEGRADED_ELIGIBLE = 2,
  INELIGIBLE = 3,
  DRAINING = 4,
  STALE = 5,
  QUARANTINED = 6,
};

constexpr std::string_view to_string(EligibilityState s) {
  switch (s) {
    case EligibilityState::ELIGIBLE: return "ELIGIBLE";
    case EligibilityState::DEGRADED_ELIGIBLE: return "DEGRADED_ELIGIBLE";
    case EligibilityState::INELIGIBLE: return "INELIGIBLE";
    case EligibilityState::DRAINING: return "DRAINING";
    case EligibilityState::STALE: return "STALE";
    case EligibilityState::QUARANTINED: return "QUARANTINED";
    case EligibilityState::UNKNOWN:
    default: return "UNKNOWN";
  }
}

/// The exact, deterministic reasons that produced an eligibility decision.
enum class EligibilityReason : std::uint8_t {
  NONE = 0,
  STALE_OBSERVATION = 1,
  ARCHITECTURE_MISMATCH = 2,
  INSUFFICIENT_MEMORY = 3,
  DRIVER_MISMATCH = 4,
  CUDA_UNAVAILABLE = 5,
  HEALTH_FAILURE = 6,
  DRAIN_POLICY = 7,
  GENERATION_MISMATCH = 8,
  WORKER_RESTART = 9,
  DEVICE_DISAPPEARANCE = 10,
  QUARANTINE = 11,
  CAPABILITY_MISMATCH = 12,
  UNKNOWN = 13,
};

constexpr std::string_view to_string(EligibilityReason r) {
  switch (r) {
    case EligibilityReason::STALE_OBSERVATION: return "stale_observation";
    case EligibilityReason::ARCHITECTURE_MISMATCH: return "architecture_mismatch";
    case EligibilityReason::INSUFFICIENT_MEMORY: return "insufficient_memory";
    case EligibilityReason::DRIVER_MISMATCH: return "driver_mismatch";
    case EligibilityReason::CUDA_UNAVAILABLE: return "cuda_unavailable";
    case EligibilityReason::HEALTH_FAILURE: return "health_failure";
    case EligibilityReason::DRAIN_POLICY: return "drain_policy";
    case EligibilityReason::GENERATION_MISMATCH: return "generation_mismatch";
    case EligibilityReason::WORKER_RESTART: return "worker_restart";
    case EligibilityReason::DEVICE_DISAPPEARANCE: return "device_disappearance";
    case EligibilityReason::QUARANTINE: return "quarantine";
    case EligibilityReason::CAPABILITY_MISMATCH: return "capability_mismatch";
    case EligibilityReason::UNKNOWN:
    default: return "unknown";
  }
}

/// Deterministic, explainable eligibility result.
struct EligibilityResult {
  EligibilityState state = EligibilityState::UNKNOWN;
  std::vector<EligibilityReason> reasons;

  bool operator==(const EligibilityResult& o) const {
    return state == o.state && reasons == o.reasons;
  }
  bool has_reason(EligibilityReason r) const {
    for (auto x : reasons) if (x == r) return true;
    return false;
  }
};

}  // namespace gpufleet
