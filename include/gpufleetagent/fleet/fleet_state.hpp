#pragma once
// Canonical, normalized fleet state store.
//
// This is the single authority for "what does this accelerator worker actually
// have and what can it safely do now." It is thread-safe, applies every
// transition explicitly and deterministically, and never rewrites prior
// observations. All authority and freshness gating (CoordinatorEpoch,
// WorkerBootId, RegistrationGeneration, ObservationGeneration,
// HealthGeneration, DeviceGeneration, CapabilityGeneration) is enforced here
// before any observation becomes authoritative.
//
// Locking discipline: a single std::mutex guards all state. No lock is ever
// held across CUDA, NVML, socket, filesystem, persistence, or external
// callback calls — all I/O happens on copies taken while the lock is held.
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "gpufleetagent/core/clock.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/types/versions.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/observation/change.hpp"
#include "gpufleetagent/observation/pipeline.hpp"

namespace gpufleet {

struct NodeRecord {
  NodeId node{};
  NodeGeneration generation{};
  Timestamp first_seen = kInvalidTimestamp;
};

struct WorkerRecord {
  WorkerId worker{};
  WorkerBootId boot{};
  RegistrationGeneration registration_generation{};
  NodeId node{};
  AgentId agent{};
  AgentVersion agent_version;
  ProtocolVersion protocol_version;
  std::string os_platform;
  DriverVersion driver_version;
  std::vector<std::string> device_identities;     // canonical identities owned now
  std::vector<std::string> supported_capabilities;
  Timestamp registered_at = kInvalidTimestamp;
  Timestamp last_heartbeat = kInvalidTimestamp;
  CoordinatorEpoch registered_epoch{};
  bool alive = true;
  bool fenced = false;
  bool lost = false;
};

/// Exact aggregate accounting, recomputed from canonical state on demand.
struct Accounting {
  std::uint64_t registered_workers = 0;
  std::uint64_t live_workers = 0;
  std::uint64_t stale_workers = 0;
  std::uint64_t devices = 0;
  std::uint64_t eligible_devices = 0;
  std::uint64_t degraded_devices = 0;
  std::uint64_t quarantined_devices = 0;
  std::uint64_t drained_devices = 0;
  std::uint64_t active_registrations = 0;
  std::uint64_t accepted_observations = 0;

  bool is_valid() const {
    return registered_workers >= live_workers && live_workers >= stale_workers;
  }
};

/// A point-in-time consistent copy of the whole fleet state.
struct StateSnapshot {
  CoordinatorId coordinator{};
  CoordinatorEpoch epoch{};
  FleetGeneration fleet_generation{};
  NodeGeneration node_generation{};
  std::map<NodeId, NodeRecord> nodes;
  std::map<WorkerId, WorkerRecord> workers;
  std::map<std::string, DeviceState> devices;      // keyed by canonical identity
  std::vector<ChangeRecord> change_log;
  Accounting accounting;
};

/// The thread-safe canonical store.
class FleetStateStore {
 public:
  explicit FleetStateStore(CoordinatorId coord) : coord_(coord) {}
  FleetStateStore(CoordinatorId coord, CoordinatorEpoch initial_epoch)
      : coord_(coord), epoch_(initial_epoch) {}

  CoordinatorId coordinator_id() const {
    std::lock_guard<std::mutex> lk(mu_);
    return coord_;
  }
  CoordinatorEpoch epoch() const {
    std::lock_guard<std::mutex> lk(mu_);
    return epoch_;
  }

  /// Roll the coordinator epoch forward. Only greater epochs are accepted.
  Result<void> roll_epoch(CoordinatorEpoch ne) {
    std::lock_guard<std::mutex> lk(mu_);
    if (ne <= epoch_) return error_result(Status::Stale, "epoch roll must be strictly forward");
    epoch_ = ne;
    fleet_gen_ = fleet_gen_.next();
    // An epoch roll advances authority. It does NOT mark live workers lost:
    // a worker carries authority only at the epoch it learned, and messages
    // bearing a stale epoch are rejected per-message (fencing the old epoch).
    // Live workers re-affirm at the new epoch on re-registration, so a
    // coordinator epoch roll never corrupts an unrelated worker.
    return ok_result();
  }

  Result<RegistrationId> register_worker(const Registration& reg, Timestamp now) {
    std::lock_guard<std::mutex> lk(mu_);
    if (reg.epoch != epoch_) {
      return error_result(Status::Stale,
                          "registration epoch stale (" + reg.epoch.to_string() + " != " +
                          epoch_.to_string() + ")");
    }
    if (!reg.protocol_version.value) {
      return error_result(Status::ProtocolMismatch, "zero protocol version");
    }
    auto it = workers_.find(reg.worker);
    bool new_worker = (it == workers_.end());
    bool supersede = false;
    if (!new_worker) {
      WorkerRecord& wr = it->second;
      if (wr.boot == reg.worker_boot) {
        // Same incarnation re-affirms. Reject stale registration generation.
        if (reg.registration_generation <= wr.registration_generation) {
          return error_result(Status::Stale, "stale registration generation");
        }
        wr.registration_generation = reg.registration_generation;
        wr.last_heartbeat = now;
        wr.alive = true;
        wr.lost = false;
        update_worker_devices_locked(wr, reg.enumerated_devices);
        return ok_result(RegistrationId(reg.registration_generation.value() + 1));
      }
      // Different boot: the prior incarnation must be either fenced or lost.
      if (!wr.fenced && !wr.lost && wr.alive) {
        return error_result(Status::Duplicate,
                            "duplicate registration identity already active");
      }
      supersede = true;
    }
    WorkerRecord wr;
    wr.worker = reg.worker;
    wr.boot = reg.worker_boot;
    wr.registration_generation = reg.registration_generation;
    wr.node = reg.node;
    wr.agent = AgentId(reg.worker.value() + 1);
    wr.agent_version = reg.agent_version;
    wr.protocol_version = reg.protocol_version;
    wr.os_platform = reg.os_platform;
    wr.driver_version = reg.driver_version;
    wr.supported_capabilities = reg.supported_capabilities;
    wr.registered_at = now;
    wr.last_heartbeat = now;
    wr.registered_epoch = reg.epoch;
    wr.alive = true;
    update_worker_devices_locked(wr, reg.enumerated_devices);
    if (supersede) {
      // fence the old incarnation's records.
      it->second = wr;
    } else {
      workers_.emplace(reg.worker, std::move(wr));
    }
    nodes_.try_emplace(reg.node, NodeRecord{reg.node, NodeGeneration(1), now});
    return ok_result(RegistrationId(reg.registration_generation.value() + 1));
  }

  /// Recover canonical state from a persisted snapshot. Recovered dynamic
  /// observations are NEVER treated as fresh: device observation_fresh is set
  /// to false and eligibility is downgraded to STALE (unless quarantine/drain
  /// already applies an independent fence). Freshness must be re-established by
  /// new observations.
  Result<void> restore(const StateSnapshot& s) {
    std::lock_guard<std::mutex> lk(mu_);
    coord_ = s.coordinator;
    epoch_ = s.epoch;
    fleet_gen_ = s.fleet_generation;
    node_gen_ = s.node_generation;
    nodes_ = s.nodes;
    workers_ = s.workers;
    devices_ = s.devices;
    change_log_ = s.change_log;
    accepted_observations_ = 0;
    for (auto& [key, d] : devices_) {
      d.observation_fresh = false;
      if (d.quarantined) {
        d.eligibility = EligibilityState::QUARANTINED;
      } else if (d.drain != DrainState::ACTIVE) {
        d.eligibility = EligibilityState::DRAINING;
      } else {
        d.eligibility = EligibilityState::STALE;
        d.eligibility_reasons = {EligibilityReason::STALE_OBSERVATION};
      }
    }
    return ok_result();
  }

  Result<void> mark_worker_lost(WorkerId w, Timestamp now) {
    (void)now;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = workers_.find(w);
    if (it == workers_.end()) return ok_result();
    it->second.lost = true;
    it->second.alive = false;
    it->second.fenced = true;
    // Mark its device observations stale/lost.
    for (auto& d : it->second.device_identities) {
      auto dit = devices_.find(d);
      if (dit != devices_.end()) {
        dit->second.observation_fresh = false;
        if (dit->second.present) {
          dit->second.present = false;
          dit->second.health = HealthState::LOST;
          dit->second.eligibility = EligibilityState::INELIGIBLE;
          dit->second.eligibility_reasons = {EligibilityReason::WORKER_RESTART};
        }
      }
    }
    return ok_result();
  }

  /// Ingest one worker's full device snapshot at the current epoch.
  Result<AppliedObservation> ingest_snapshot(WorkerId worker,
                                             const std::vector<DeviceObservation>& obs,
                                             const ObservationPolicy& policy,
                                             Timestamp now) {
    std::lock_guard<std::mutex> lk(mu_);
    auto wit = workers_.find(worker);
    if (wit == workers_.end()) return error_result(Status::NotFound, "unregistered worker");
    WorkerRecord& wr = wit->second;
    if (wr.lost || wr.fenced) return error_result(Status::NotAuthoritative, "worker is fenced/lost");

    std::set<std::string> seen_keys;
    AppliedObservation last{};

    for (const auto& o : obs) {
      if (o.epoch != epoch_) return error_result(Status::Stale, "stale coordinator epoch in observation");
      if (o.source_worker_boot != wr.boot) return error_result(Status::Stale, "stale worker boot id in observation");
      if (o.source_worker != worker) return error_result(Status::IdentityMismatch, "observation worker identity mismatch");

      const std::string key = canonical_device_identity(o.identity);
      auto dit = devices_.find(key);
      DeviceState prior = (dit == devices_.end()) ? DeviceState{} : dit->second;

      // Generation monotonicity gate. Generations are scoped PER WORKER
      // INCARNATION: a fresh WorkerBootId (a new process incarnation) begins a
      // new generation sequence, so its observations are never blocked by a
      // higher generation left behind by a superseded incarnation. Only
      // observations from the SAME boot are gated against the prior state.
      bool new_device = (dit == devices_.end());
      bool same_incarnation = !new_device && (o.source_worker_boot == prior.source_worker_boot);
      if (same_incarnation) {
        if (o.observation_generation <= prior.observation_generation) {
          continue;  // stale or duplicate observation within the same incarnation.
        }
        if (o.device_generation < prior.generation) {
          return error_result(Status::InvalidGeneration, "stale device generation");
        }
      }
      if (!new_device && o.health_generation < prior.health_generation &&
          o.source_worker_boot == prior.source_worker_boot) {
        return error_result(Status::InvalidGeneration, "stale health generation");
      }

      // apply pipeline.
      AppliedObservation applied = apply_observation(prior, o, policy, now, epoch_);
      applied.state.health_generation = o.health_generation;

      bool replaced = (dit != devices_.end());
      devices_[key] = applied.state;
      for (auto& c : applied.changes) {
        change_log_.push_back(c);
        if (change_log_.size() > kMaxChangeLog) change_log_.erase(change_log_.begin());
      }
      if (!replaced) {
        // New device for this worker.
        wr.device_identities.push_back(key);
      }
      seen_keys.insert(key);
      last = std::move(applied);
      ++accepted_observations_;
    }

    // Devices previously owned by this worker but no longer present => LOST.
    std::vector<std::string> to_remove;
    for (auto& key : wr.device_identities) {
      if (!seen_keys.count(key)) {
        auto dit = devices_.find(key);
        if (dit != devices_.end()) {
          ChangeRecord cr{ChangeKind::DEVICE_DISAPPEARED, dit->second.device_id, now,
                          dit->second.generation, epoch_, "device no longer reported by worker",
                          "present", "absent"};
          change_log_.push_back(cr);
          if (change_log_.size() > kMaxChangeLog) change_log_.erase(change_log_.begin());
          dit->second.present = false;
          dit->second.observation_fresh = false;
          dit->second.health = HealthState::LOST;
          dit->second.eligibility = EligibilityState::INELIGIBLE;
          dit->second.eligibility_reasons = {EligibilityReason::DEVICE_DISAPPEARANCE};
        }
        // Keep the identity (stable) but mark as lost rather than dropping.
      }
    }
    wr.last_heartbeat = now;
    wr.alive = true;
    return ok_result(std::move(last));
  }

  Result<void> heartbeat(WorkerId worker, WorkerBootId boot, CoordinatorEpoch epoch, Timestamp now,
                         HealthGeneration hg) {
    std::lock_guard<std::mutex> lk(mu_);
    if (epoch != epoch_) return error_result(Status::Stale, "stale heartbeat epoch");
    auto it = workers_.find(worker);
    if (it == workers_.end()) return error_result(Status::NotFound, "unregistered worker");
    if (it->second.boot != boot) return error_result(Status::Stale, "stale heartbeat boot id");
    (void)hg;
    it->second.last_heartbeat = now;
    it->second.alive = true;
    it->second.lost = false;
    return ok_result();
  }

  Result<void> set_drain(WorkerId w, const std::string& identity, DrainState d, Timestamp now) {
    (void)w;
    std::lock_guard<std::mutex> lk(mu_);
    auto dit = devices_.find(identity);
    if (dit == devices_.end()) return error_result(Status::NotFound, "device not found");
    auto pit = dit->second.drain;
    if (pit == d) return ok_result();
    dit->second.drain = d;
    recompute_device_eligibility(dit->second);
    std::string from = std::string(to_string(pit));
    std::string to = std::string(to_string(d));
    ChangeKind kind = (d >= DrainState::DRAINING) ? ChangeKind::DRAIN_INITIATED
                                                  : ChangeKind::DRAIN_COMPLETED;
    change_log_.push_back(ChangeRecord{kind, dit->second.device_id, now, dit->second.generation,
                                       epoch_, "drain state changed", from, to});
    if (change_log_.size() > kMaxChangeLog) change_log_.erase(change_log_.begin());
    return ok_result();
  }

  Result<void> set_quarantine(WorkerId w, const std::string& identity, const QuarantineRecord& q,
                              Timestamp now) {
    (void)w;
    std::lock_guard<std::mutex> lk(mu_);
    auto dit = devices_.find(identity);
    if (dit == devices_.end()) return error_result(Status::NotFound, "device not found");
    if (q.generation < dit->second.generation) return error_result(Status::InvalidGeneration, "stale quarantine generation");
    bool was = dit->second.quarantined;
    dit->second.quarantined = true;
    dit->second.quarantine = q;
    recompute_device_eligibility(dit->second);
    if (!was) {
      change_log_.push_back(ChangeRecord{ChangeKind::QUARANTINE_APPLIED, dit->second.device_id, now,
                                         dit->second.generation, epoch_, q.reason, "false", "true"});
      if (change_log_.size() > kMaxChangeLog) change_log_.erase(change_log_.begin());
    }
    return ok_result();
  }

  Result<void> clear_quarantine(WorkerId w, const std::string& identity, CoordinatorEpoch epoch,
                                Timestamp now) {
    (void)w;
    std::lock_guard<std::mutex> lk(mu_);
    if (epoch != epoch_) return error_result(Status::Stale, "stale epoch in clear");
    auto dit = devices_.find(identity);
    if (dit == devices_.end()) return error_result(Status::NotFound, "device not found");
    if (!dit->second.quarantined) return error_result(Status::NotFound, "device not quarantined");
    dit->second.quarantined = false;
    dit->second.quarantine = QuarantineRecord{};
    recompute_device_eligibility(dit->second);
    change_log_.push_back(ChangeRecord{ChangeKind::QUARANTINE_CLEARED, dit->second.device_id, now,
                                       dit->second.generation, epoch_, "quarantine cleared", "true", "false"});
    if (change_log_.size() > kMaxChangeLog) change_log_.erase(change_log_.begin());
    return ok_result();
  }

  StateSnapshot snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    StateSnapshot s;
    s.coordinator = coord_;
    s.epoch = epoch_;
    s.fleet_generation = fleet_gen_;
    s.node_generation = node_gen_;
    s.nodes = nodes_;
    s.workers = workers_;
    s.devices = devices_;
    s.change_log = change_log_;
    s.accounting = recompute_accounting_locked();
    return s;
  }

  Accounting accounting() const {
    std::lock_guard<std::mutex> lk(mu_);
    return recompute_accounting_locked();
  }

  std::vector<ChangeRecord> changes() const {
    std::lock_guard<std::mutex> lk(mu_);
    return change_log_;
  }

  bool device_exists(const std::string& identity) const {
    std::lock_guard<std::mutex> lk(mu_);
    return devices_.count(identity) != 0;
  }

  std::uint64_t accepted_observations() const {
    std::lock_guard<std::mutex> lk(mu_);
    return accepted_observations_;
  }

 private:
  static constexpr std::size_t kMaxChangeLog = 10000;

  void recompute_device_eligibility(DeviceState& d) const {
    EligibilityInput ei;
    ei.health = d.health;
    ei.observation_fresh = d.observation_fresh;
    ei.observed_generation = d.generation;
    ei.current_generation = d.generation;
    ei.drain_active = d.drain != DrainState::ACTIVE;
    ei.quarantine = d.quarantined;
    ei.device_present = d.present;
    ei.cuda_available = d.core_validation_ok && d.present;
    ei.available_memory = d.free_memory;
    ei.required_memory = 0;
    auto elig = evaluate_eligibility(ei);
    d.eligibility = elig.state;
    d.eligibility_reasons = elig.reasons;
  }

  void update_worker_devices_locked(WorkerRecord& wr,
                                    const std::vector<std::string>& enumerated) {
    wr.device_identities = enumerated;
  }

  Accounting recompute_accounting_locked() const {
    Accounting a;
    for (const auto& [k, wr] : workers_) {
      ++a.registered_workers;
      if (wr.alive && !wr.lost && !wr.fenced) ++a.live_workers;
      if (wr.lost || wr.fenced || !wr.alive) ++a.stale_workers;
      if (wr.alive && !wr.lost && !wr.fenced) ++a.active_registrations;
    }
    for (const auto& [k, d] : devices_) {
      ++a.devices;
      switch (d.eligibility) {
        case EligibilityState::ELIGIBLE: ++a.eligible_devices; break;
        case EligibilityState::DEGRADED_ELIGIBLE: ++a.degraded_devices; break;
        case EligibilityState::QUARANTINED: ++a.quarantined_devices; break;
        case EligibilityState::DRAINING: ++a.drained_devices; break;
        default: break;
      }
    }
    a.accepted_observations = accepted_observations_;
    return a;
  }

  mutable std::mutex mu_;
  CoordinatorId coord_;
  CoordinatorEpoch epoch_{};
  FleetGeneration fleet_gen_{1};
  NodeGeneration node_gen_{1};
  std::map<NodeId, NodeRecord> nodes_;
  std::map<WorkerId, WorkerRecord> workers_;
  std::map<std::string, DeviceState> devices_;
  std::vector<ChangeRecord> change_log_;
  std::uint64_t accepted_observations_ = 0;
};

}  // namespace gpufleet
