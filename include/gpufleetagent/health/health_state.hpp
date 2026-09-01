#pragma once
// Typed health states.
//
// A device is not HEALTHY merely because the OS can enumerate it. Health is
// derived from an explicit, deterministic evaluation over observables:
// enumeration, driver/runtime availability, CUDA initialization, memory
// allocation, kernel execution, synchronization, memory round-trip, fatal
// error state, temperature/power constraints, repeated operation failures,
// stale observations, and administrative drain/quarantine.
#include <cstdint>
#include <string_view>

namespace gpufleet {

enum class HealthState : std::uint8_t {
  UNKNOWN = 0,
  HEALTHY = 1,
  DEGRADED = 2,
  UNHEALTHY = 3,
  DRAINING = 4,
  DRAINED = 5,
  OFFLINE = 6,
  LOST = 7,
  RECOVERING = 8,
  QUARANTINED = 9,
};

constexpr std::string_view to_string(HealthState s) {
  switch (s) {
    case HealthState::UNKNOWN: return "UNKNOWN";
    case HealthState::HEALTHY: return "HEALTHY";
    case HealthState::DEGRADED: return "DEGRADED";
    case HealthState::UNHEALTHY: return "UNHEALTHY";
    case HealthState::DRAINING: return "DRAINING";
    case HealthState::DRAINED: return "DRAINED";
    case HealthState::OFFLINE: return "OFFLINE";
    case HealthState::LOST: return "LOST";
    case HealthState::RECOVERING: return "RECOVERING";
    case HealthState::QUARANTINED: return "QUARANTINED";
    default: return "UNKNOWN";
  }
}

/// A device whose health is QUARANTINED is not health-so-far-but-flagged: it
/// is forcibly ineligible until explicitly and validly cleared.
constexpr bool is_health_availability_gating(HealthState s) noexcept {
  return s == HealthState::QUARANTINED || s == HealthState::DRAINED ||
         s == HealthState::DRAINING || s == HealthState::OFFLINE ||
         s == HealthState::LOST || s == HealthState::UNHEALTHY ||
         s == HealthState::UNKNOWN;
}

}  // namespace gpufleet
