#pragma once
// The observation pipeline: enumerate -> identify -> probe -> validate ->
// normalize -> compare -> publish -> commit.
//
// This header implements the deterministic pure stages that run inside the
// coordinator/agent state machine: normalizing a raw backend probe into a
// typed observation, and then applying that normalized observation to the
// prior authoritative device state to produce a new authoritative state plus a
// deterministic set of change records. Publication/commit atomicity is
// provided by the fleet state store, which swaps the whole snapshot at once.
#include <cstdint>
#include <string>
#include <vector>

#include "gpufleetagent/core/clock.hpp"
#include "gpufleetagent/device/device_backend.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/observation/change.hpp"
#include "gpufleetagent/health/health_evaluator.hpp"
#include "gpufleetagent/eligibility/eligibility_evaluator.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

namespace gpufleet {

enum class PipelineStage : std::uint8_t {
  Enumerate = 1,
  Identify = 2,
  Probe = 3,
  Validate = 4,
  Normalize = 5,
  Compare = 6,
  Publish = 7,
  Commit = 8,
};

constexpr std::string_view to_string(PipelineStage s) {
  switch (s) {
    case PipelineStage::Enumerate: return "enumerate";
    case PipelineStage::Identify: return "identify";
    case PipelineStage::Probe: return "probe";
    case PipelineStage::Validate: return "validate";
    case PipelineStage::Normalize: return "normalize";
    case PipelineStage::Compare: return "compare";
    case PipelineStage::Publish: return "publish";
    case PipelineStage::Commit: return "commit";
    default: return "unknown";
  }
}

/// Metadata that contextualizes a single observation (who observed it, when,
/// and at what generation/epoch). This is carried with every normalized
/// observation so that authority and freshness can be checked deterministically.
struct ObservationMetadata {
  DeviceId device_id{};
  ObservationGeneration observation_generation{};
  HealthGeneration health_generation{};
  Timestamp observed_at = kInvalidTimestamp;
  WorkerBootId source_worker_boot{};
  WorkerId source_worker{};
  NodeId source_node{};
  CoordinatorEpoch epoch{};
  DeviceGeneration device_generation{};
};

/// Policy that governs freshness, required architecture and memory, and the
/// strictness of validation-based eligibility.
struct ObservationPolicy {
  DurationMs freshness_threshold_ms = 24 * 60 * 60 * 1000LL;  // 24h default
  std::string required_architecture;                // empty => any
  ComputeCapability required_compute_capability{};  // zero => any
  std::uint64_t required_memory = 0;                // minimum total memory bytes
  bool require_core_validation = true;              // eligibility requires validation
  std::vector<std::string> required_capabilities;   // empty => none required
};

/// Normalize a raw backend probe into a typed DeviceObservation. Pure; no I/O.
inline DeviceObservation normalize_observation(const DeviceProbe& probe,
                                              const ObservationMetadata& meta) {
  DeviceObservation o;
  o.device_id = meta.device_id;
  o.observation_generation = meta.observation_generation;
  o.observed_at = meta.observed_at;
  o.source_worker_boot = meta.source_worker_boot;
  o.source_worker = meta.source_worker;
  o.source_node = meta.source_node;
  o.epoch = meta.epoch;
  o.device_generation = meta.device_generation;
  o.health_generation = meta.health_generation;
  o.identity = probe.identity;
  o.enumerated = probe.enumerated;
  o.present = probe.present;
  o.driver_runtime_ok = probe.driver_runtime_ok;
  o.cuda_init_ok = probe.cuda_init_ok;
  o.total_memory = probe.total_memory;
  o.used_memory = probe.used_memory;
  o.free_memory = probe.free_memory;
  o.temperature_c = probe.temperature_c;
  o.power_w = probe.power_w;
  o.capabilities = probe.capabilities;
  o.core_validation_ok = probe.core_validation_ok;
  o.validation_detail = probe.validation_detail;
  o.memory_alloc_ok = probe.memory_alloc_ok;
  o.h2d_ok = probe.h2d_ok;
  o.kernel_exec_ok = probe.kernel_exec_ok;
  o.sync_ok = probe.sync_ok;
  o.d2h_ok = probe.d2h_ok;
  o.verify_ok = probe.verify_ok;
  o.health = probe.present ? HealthState::UNKNOWN : HealthState::OFFLINE;
  return o;
}

/// Build the per-device physical health signals from a normalized observation
/// plus prior authoritative presence knowledge.
inline HealthSignals health_signals_from_probe(const DeviceObservation& probe, bool was_known,
                                                bool observation_stale) {
  HealthSignals s;
  s.enumerated = probe.enumerated;
  s.device_present = probe.present;
  s.was_known = was_known;
  s.driver_runtime_ok = probe.driver_runtime_ok;
  s.cuda_init_ok = probe.cuda_init_ok;
  s.memory_alloc_ok = probe.memory_alloc_ok;
  s.kernel_exec_ok = probe.kernel_exec_ok;
  s.sync_ok = probe.sync_ok;
  s.mem_roundtrip_ok = probe.d2h_ok && probe.verify_ok;
  s.fatal_error = false;  // not currently surfaced through the probe; reserved
  s.consecutive_failures = probe.core_validation_ok ? 0 : 1;
  s.observation_stale = observation_stale;
  s.last_validation_ok = probe.core_validation_ok;
  return s;
}

/// The result of applying a normalized observation to prior authoritative
/// state: the new authoritative state plus the deterministic changes.
struct AppliedObservation {
  DeviceState state;
  std::vector<ChangeRecord> changes;
  EligibilityResult eligibility;
  bool fresh = false;
  bool accepted = false;
};

/// Apply a normalized observation to prior authoritative device state and
/// produce the new authoritative state, eligibility, and change records.
/// Pure; no time reads, no I/O. p now is the caller's clock value.
inline AppliedObservation apply_observation(const DeviceState& prior,
                                            const DeviceObservation& norm,
                                            const ObservationPolicy& policy,
                                            Timestamp now,
                                            CoordinatorEpoch epoch) {
  AppliedObservation out;

  const bool fresh = (norm.observed_at != kInvalidTimestamp) &&
                     (now - norm.observed_at) <= policy.freshness_threshold_ms &&
                     (norm.observed_at >= prior.last_observed_at || prior.last_observed_at == kInvalidTimestamp);

  // Physical health is computed purely from probe signals plus prior presence
  // knowledge and the freshness flag. Administrative drain/quarantine are NOT
  // passed into physical health; they are modelled as separate axes and applied
  // to eligibility below.
  HealthSignals hs = health_signals_from_probe(norm, prior.present, !fresh);
  if (!norm.present) hs.device_present = false;
  HealthResult health = evaluate_health(hs);

  DeviceState cand;
  cand.device_id = norm.device_id;
  cand.generation = norm.device_generation;
  cand.identity = norm.identity;
  cand.present = norm.present;
  cand.health = health.state;
  cand.health_explanation = health.explanation;
  cand.total_memory = norm.total_memory;
  cand.used_memory = norm.used_memory;
  cand.free_memory = norm.free_memory;
  cand.temperature_c = norm.temperature_c;
  cand.power_w = norm.power_w;
  cand.capabilities = norm.capabilities;
  cand.core_validation_ok = norm.core_validation_ok;
  cand.validation_detail = norm.validation_detail;
  cand.last_observed_at = norm.observed_at;
  if (norm.core_validation_ok) cand.last_validated_at = norm.observed_at;
  else cand.last_validated_at = prior.last_validated_at;
  cand.observation_generation = norm.observation_generation;
  cand.source_worker_boot = norm.source_worker_boot;
  cand.observation_fresh = fresh;

  // Administrative axes are preserved across observations (only admin changes
  // them).
  cand.drain = prior.drain;
  cand.quarantined = prior.quarantined;
  cand.quarantine = prior.quarantine;

  // Compute explainable eligibility.
  EligibilityInput ei;
  ei.health = cand.health;
  ei.observation_fresh = fresh;
  ei.observed_generation = cand.generation;
  ei.current_generation = cand.generation;
  ei.architecture_match = policy.required_architecture.empty() ||
                          cand.identity.architecture == policy.required_architecture;
  if (cand.identity.compute_capability != ComputeCapability{} &&
      policy.required_compute_capability != ComputeCapability{}) {
    ei.architecture_match = ei.architecture_match &&
                            cand.identity.compute_capability >= policy.required_compute_capability;
  }
  ei.sufficient_memory = cand.total_memory >= policy.required_memory;
  ei.driver_match = true;  // no driver text requirement by default
  ei.cuda_available = norm.driver_runtime_ok && norm.cuda_init_ok;
  ei.capability_match = policy.required_capabilities.empty();
  for (auto& req : policy.required_capabilities) {
    bool has = false;
    for (auto& c : cand.capabilities) if (c.name == req && c.kind != CapabilityKind::UNKNOWN) { has = true; break; }
    if (!has) { ei.capability_match = false; break; }
  }
  ei.drain_active = cand.drain != DrainState::ACTIVE;
  ei.quarantine = cand.quarantined;
  ei.worker_alive = true;   // set by transport layer when it accepts the frame
  ei.device_present = cand.present;
  ei.required_memory = policy.required_memory;
  ei.available_memory = cand.free_memory;

  EligibilityResult elig = evaluate_eligibility(ei);
  cand.eligibility = elig.state;
  cand.eligibility_reasons = elig.reasons;

  if (fresh && cand.eligibility != EligibilityState::STALE &&
      cand.eligibility != EligibilityState::QUARANTINED &&
      cand.eligibility != EligibilityState::DRAINING) {
    cand.last_authoritative_at = now;
  } else {
    cand.last_authoritative_at = prior.last_authoritative_at;
  }

  out.state = cand;
  out.eligibility = elig;
  out.fresh = fresh;
  out.accepted = true;
  out.changes = diff_device_state(prior, cand, norm.observed_at, cand.generation, epoch);
  return out;
}

}  // namespace gpufleet
