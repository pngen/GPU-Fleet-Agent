#pragma once
// Deterministic device change detection.
//
// Observations are immutable; change is derived by comparing a candidate
// normalized state against the prior authoritative state. Every transition is
// emitted as a deterministic ChangeRecord. Prior observations are never
// rewritten.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gpufleetagent/core/clock.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/observation/observation.hpp"

namespace gpufleet {

enum class ChangeKind : std::uint8_t {
  DEVICE_APPEARED = 1,
  DEVICE_DISAPPEARED = 2,
  IDENTITY_CHANGED = 3,
  DRIVER_RUNTIME_CHANGED = 4,
  MEMORY_CAPACITY_CHANGED = 5,
  HEALTH_CHANGED = 6,
  CAPABILITY_CHANGED = 7,
  ELIGIBILITY_CHANGED = 8,
  WORKER_RESTARTED = 9,
  OBSERVATION_STALE = 10,
  DRAIN_INITIATED = 11,
  DRAIN_COMPLETED = 12,
  QUARANTINE_APPLIED = 13,
  QUARANTINE_CLEARED = 14,
};

constexpr std::string_view to_string(ChangeKind k) {
  switch (k) {
    case ChangeKind::DEVICE_APPEARED: return "device_appeared";
    case ChangeKind::DEVICE_DISAPPEARED: return "device_disappeared";
    case ChangeKind::IDENTITY_CHANGED: return "identity_changed";
    case ChangeKind::DRIVER_RUNTIME_CHANGED: return "driver_runtime_changed";
    case ChangeKind::MEMORY_CAPACITY_CHANGED: return "memory_capacity_changed";
    case ChangeKind::HEALTH_CHANGED: return "health_changed";
    case ChangeKind::CAPABILITY_CHANGED: return "capability_changed";
    case ChangeKind::ELIGIBILITY_CHANGED: return "eligibility_changed";
    case ChangeKind::WORKER_RESTARTED: return "worker_restarted";
    case ChangeKind::OBSERVATION_STALE: return "observation_stale";
    case ChangeKind::DRAIN_INITIATED: return "drain_initiated";
    case ChangeKind::DRAIN_COMPLETED: return "drain_completed";
    case ChangeKind::QUARANTINE_APPLIED: return "quarantine_applied";
    case ChangeKind::QUARANTINE_CLEARED: return "quarantine_cleared";
    default: return "unknown_change";
  }
}

struct ChangeRecord {
  ChangeKind kind = ChangeKind::DEVICE_APPEARED;
  DeviceId device_id{};
  Timestamp at = kInvalidTimestamp;
  DeviceGeneration generation{};
  CoordinatorEpoch epoch{};
  std::string detail;
  std::string from;
  std::string to;
};

/// Deterministic diff between prior and candidate device state. Pure; no I/O.
/// The candidate is the *normalized* new state (not a raw observation); prior
/// is the last authoritative state. This function emits every changed axis.
inline std::vector<ChangeRecord> diff_device_state(const DeviceState& prior,
                                                   const DeviceState& cand,
                                                   Timestamp at,
                                                   DeviceGeneration gen,
                                                   CoordinatorEpoch epoch) {
  std::vector<ChangeRecord> out;
  auto push = [&](ChangeKind k, std::string detail, std::string from, std::string to) {
    out.push_back(ChangeRecord{k, cand.device_id, at, gen, epoch, std::move(detail),
                               std::move(from), std::move(to)});
  };

  if (!prior.present && cand.present) {
    push(ChangeKind::DEVICE_APPEARED, "device became present", "absent", "present");
  } else if (prior.present && !cand.present) {
    push(ChangeKind::DEVICE_DISAPPEARED, "device became absent", "present", "absent");
  }

  if (prior.identity != cand.identity) {
    push(ChangeKind::IDENTITY_CHANGED, "device identity changed",
         prior.identity.uuid.to_string(), cand.identity.uuid.to_string());
  }
  if (prior.identity.driver_version.text != cand.identity.driver_version.text) {
    push(ChangeKind::DRIVER_RUNTIME_CHANGED, "driver/runtime changed",
         prior.identity.driver_version.text, cand.identity.driver_version.text);
  }
  if (prior.total_memory != cand.total_memory && cand.total_memory != 0) {
    push(ChangeKind::MEMORY_CAPACITY_CHANGED, "memory capacity changed",
         std::to_string(prior.total_memory), std::to_string(cand.total_memory));
  }

  if (prior.health != cand.health) {
    push(ChangeKind::HEALTH_CHANGED, "health changed", std::string(to_string(prior.health)),
         std::string(to_string(cand.health)));
  }

  auto caps_key = [](const std::vector<Capability>& c) {
    std::string s;
    for (auto& x : c) { s += x.name; s += '='; s += x.value; s += ';'; }
    return s;
  };
  if (caps_key(prior.capabilities) != caps_key(cand.capabilities)) {
    push(ChangeKind::CAPABILITY_CHANGED, "capability set changed",
         caps_key(prior.capabilities), caps_key(cand.capabilities));
  }

  if (prior.eligibility != cand.eligibility) {
    push(ChangeKind::ELIGIBILITY_CHANGED, "eligibility changed",
         std::string(to_string(prior.eligibility)), std::string(to_string(cand.eligibility)));
  }

  if (prior.source_worker_boot != cand.source_worker_boot) {
    push(ChangeKind::WORKER_RESTARTED, "source worker incarnation changed",
         prior.source_worker_boot.to_string(), cand.source_worker_boot.to_string());
  }

  if (!prior.observation_fresh && cand.observation_fresh) {
    // transitioned from stale/not-fresh to fresh is not itself a stale event.
  } else if (prior.observation_fresh && !cand.observation_fresh) {
    push(ChangeKind::OBSERVATION_STALE, "observation became stale", "fresh", "stale");
  }

  if (prior.drain == DrainState::ACTIVE && cand.drain != DrainState::ACTIVE) {
    push(ChangeKind::DRAIN_INITIATED, "drain initiated",
         std::string(to_string(prior.drain)), std::string(to_string(cand.drain)));
  } else if (prior.drain == DrainState::DRAINING && cand.drain == DrainState::DRAINED) {
    push(ChangeKind::DRAIN_COMPLETED, "drain completed",
         std::string(to_string(prior.drain)), std::string(to_string(cand.drain)));
  }

  if (!prior.quarantined && cand.quarantined) {
    push(ChangeKind::QUARANTINE_APPLIED, "quarantine applied", "false", "true");
  } else if (prior.quarantined && !cand.quarantined) {
    push(ChangeKind::QUARANTINE_CLEARED, "quarantine cleared", "true", "false");
  }

  return out;
}

}  // namespace gpufleet
