#pragma once
// Observation and canonical device-state model.
//
// A DeviceObservation is one published frame of evidence about a device from a
// specific worker incarnation. A DeviceState is the coordinator's canonical,
// authoritative view of a device, derived by normalizing observations and
// applying freshness, health, eligibility, drain, and quarantine semantics.
// Prior observations are never rewritten; change detection is derived by
// comparing a new observation against the prior authoritative state.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gpufleetagent/core/clock.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/identity/device_identity.hpp"
#include "gpufleetagent/health/health_state.hpp"
#include "gpufleetagent/capability/capability.hpp"
#include "gpufleetagent/eligibility/eligibility.hpp"

namespace gpufleet {

/// Administrative drain state. A device/node moves ACTIVE -> DRAINING ->
/// DRAINED, rejecting new eligibility while preserving authoritative state.
enum class DrainState : std::uint8_t {
  ACTIVE = 0,
  DRAINING = 1,
  DRAINED = 2,
};

constexpr std::string_view to_string(DrainState s) {
  switch (s) {
    case DrainState::ACTIVE: return "ACTIVE";
    case DrainState::DRAINING: return "DRAINING";
    case DrainState::DRAINED: return "DRAINED";
    default: return "ACTIVE";
  }
}

/// A quarantine record. Quarantine is independent from drain.
struct QuarantineRecord {
  std::string reason;
  std::string source;                 // who/what applied it
  DeviceGeneration generation{};
  Timestamp at = kInvalidTimestamp;
  DeviceId device_id{};
  CoordinatorEpoch authority{};
  bool operator==(const QuarantineRecord& o) const {
    return reason == o.reason && source == o.source && generation == o.generation &&
           at == o.at && device_id == o.device_id && authority == o.authority;
  }
};

/// One published observation frame from a worker incarnation.
struct DeviceObservation {
  DeviceId device_id{};
  ObservationGeneration observation_generation{};
  HealthGeneration health_generation{};
  Timestamp observed_at = kInvalidTimestamp;
  WorkerBootId source_worker_boot{};
  WorkerId source_worker{};
  NodeId source_node{};
  CoordinatorEpoch epoch{};
  DeviceGeneration device_generation{};
  DeviceIdentity identity;
  HealthState health = HealthState::UNKNOWN;
  std::string health_explanation;
  std::uint64_t total_memory = 0;
  std::uint64_t used_memory = 0;
  std::uint64_t free_memory = 0;
  std::optional<double> temperature_c;
  std::optional<double> power_w;
  std::vector<Capability> capabilities;
  bool core_validation_ok = false;
  std::string validation_detail;
  bool enumerated = false;
  bool present = true;
  bool driver_runtime_ok = false;
  bool cuda_init_ok = false;
  // Per-step validation evidence mirrored from the probe.
  bool memory_alloc_ok = false;
  bool h2d_ok = false;
  bool kernel_exec_ok = false;
  bool sync_ok = false;
  bool d2h_ok = false;
  bool verify_ok = false;
};

/// Canonical, authoritative per-device state maintained by the coordinator.
/// This is what downstream runtimes consume for placement/admission.
struct DeviceState {
  DeviceId device_id{};
  DeviceGeneration generation{};
  DeviceIdentity identity;
  bool present = false;

  HealthState health = HealthState::UNKNOWN;
  std::string health_explanation;

  EligibilityState eligibility = EligibilityState::UNKNOWN;
  std::vector<EligibilityReason> eligibility_reasons;

  DrainState drain = DrainState::ACTIVE;
  bool quarantined = false;
  QuarantineRecord quarantine;

  std::uint64_t total_memory = 0;
  std::uint64_t used_memory = 0;
  std::uint64_t free_memory = 0;
  std::optional<double> temperature_c;
  std::optional<double> power_w;

  std::vector<Capability> capabilities;
  bool core_validation_ok = false;
  std::string validation_detail;

  Timestamp last_observed_at = kInvalidTimestamp;
  Timestamp last_validated_at = kInvalidTimestamp;
  Timestamp last_authoritative_at = kInvalidTimestamp;  // last time it was fresh/canonical
  ObservationGeneration observation_generation{};
  HealthGeneration health_generation{};
  WorkerBootId source_worker_boot{};
  bool observation_fresh = false;
};

}  // namespace gpufleet
