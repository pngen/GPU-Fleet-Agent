#include "tests/test_fw.hpp"
#include "gpufleetagent/device/device_backend.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/observation/observation_codec.hpp"

using namespace gpufleet;

static DeviceProbe make_probe() {
  DeviceProbe p;
  p.enumerated = true; p.present = true;
  p.driver_runtime_ok = true; p.cuda_init_ok = true;
  p.memory_alloc_ok = true; p.h2d_ok = true; p.kernel_exec_ok = true;
  p.sync_ok = true; p.d2h_ok = true; p.verify_ok = true; p.core_validation_ok = true;
  p.total_memory = 32ull*1024*1024*1024; p.free_memory = 24ull*1024*1024*1024; p.used_memory = 8ull*1024*1024*1024;
  p.identity.vendor = AcceleratorVendor::Nvidia;
  p.identity.vendor_name = "NVIDIA";
  p.identity.architecture = "blackwell";
  p.identity.compute_capability = ComputeCapability{12,0};
  p.identity.total_physical_memory = p.total_memory;
  p.identity.driver_version.text = "13040";
  return p;
}

int main() {
  ObservationPolicy policy;
  policy.freshness_threshold_ms = 1000;
  policy.required_architecture = "blackwell";

  DeviceProbe probe = make_probe();
  ObservationMetadata meta;
  meta.device_id = DeviceId(1u);
  meta.observation_generation = ObservationGeneration(1u);
  meta.health_generation = HealthGeneration(1u);
  meta.observed_at = 1000;
  meta.source_worker_boot = WorkerBootId(5u);
  meta.source_worker = WorkerId(2u);
  meta.source_node = NodeId(3u);
  meta.epoch = CoordinatorEpoch(1u);
  meta.device_generation = DeviceGeneration(1u);

  DeviceObservation o = normalize_observation(probe, meta);
  CHECK(o.present);
  CHECK((o.identity.compute_capability == ComputeCapability{12,0}));
  CHECK(o.health == HealthState::UNKNOWN || o.health == HealthState::HEALTHY);

  // Apply at now = 1100 (within 1000ms threshold -> fresh).
  DeviceState prior;  // empty prior => new device
  AppliedObservation a = apply_observation(prior, o, policy, 1100, CoordinatorEpoch(1u));
  CHECK(a.fresh);
  CHECK(a.state.eligibility == EligibilityState::ELIGIBLE);
  CHECK(a.state.observation_fresh);
  CHECK(a.state.health == HealthState::HEALTHY);

  // The new-device diff should include DEVICE_APPEARED.
  bool appeared = false;
  for (auto& c : a.changes) if (c.kind == ChangeKind::DEVICE_APPEARED) appeared = true;
  CHECK(appeared);

  // Now observe again but with the observation timestamp far in the past (stale).
  // The observation generation must advance.
  DeviceObservation o2 = o;
  o2.observation_generation = ObservationGeneration(2u);
  o2.observed_at = 800;  // 300ms before now=1100? no; use now such that it is stale.
  // Apply at now=3000, threshold 1000 => stale.
  AppliedObservation b = apply_observation(a.state, o2, policy, 3000, CoordinatorEpoch(1u));
  CHECK(!b.fresh);
  CHECK(b.state.eligibility == EligibilityState::STALE);
  CHECK(b.state.observation_fresh == false);

  // A stale observation must never yield ELIGIBLE even if health is healthy.
  CHECK(b.state.eligibility != EligibilityState::ELIGIBLE);

  // Observation batch codec round trip.
  {
    std::vector<DeviceObservation> batch{o};
    auto bb = encode_observation_batch(batch);
    auto db = decode_observation_batch(bb);
    CHECK(db.ok());
    CHECK(db.ok() && db.value().size() == 1u);
    if (db.ok() && db.value().size() == 1u) {
      CHECK(db.value()[0].device_id == o.device_id);
      CHECK(db.value()[0].identity.compute_capability == o.identity.compute_capability);
      CHECK(db.value()[0].core_validation_ok);
    }
  }

  // Architecture mismatch: policy requires a different architecture.
  ObservationPolicy p2 = policy;
  p2.required_architecture = "hopper";
  AppliedObservation c2 = apply_observation(prior, o, p2, 1100, CoordinatorEpoch(1u));
  CHECK(c2.state.eligibility == EligibilityState::INELIGIBLE);
  CHECK(c2.state.eligibility_reasons.size() > 0);

  // Insufficient memory.
  ObservationPolicy p3 = policy;
  p3.required_memory = 128ull*1024*1024*1024;
  AppliedObservation c3 = apply_observation(prior, o, p3, 1100, CoordinatorEpoch(1u));
  CHECK(c3.state.eligibility == EligibilityState::INELIGIBLE);

  return tf::summary("observation");
}
