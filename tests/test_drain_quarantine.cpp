#include "tests/test_fw.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"

using namespace gpufleet;

static DeviceObservation healthy_obs(WorkerBootId boot, DeviceGeneration dg, ObservationGeneration og) {
  DeviceObservation o;
  o.device_id = DeviceId(1u);
  o.observation_generation = og;
  o.health_generation = HealthGeneration(1u);
  o.observed_at = 5000;
  o.source_worker_boot = boot;
  o.source_worker = WorkerId(10u);
  o.source_node = NodeId(1u);
  o.epoch = CoordinatorEpoch(1u);
  o.device_generation = dg;
  o.identity.vendor = AcceleratorVendor::Nvidia;
  o.identity.vendor_name = "NVIDIA";
  o.identity.architecture = "blackwell";
  o.identity.compute_capability = ComputeCapability{12,0};
  o.identity.total_physical_memory = 32ull*1024*1024*1024;
  o.identity.driver_version.text = "13040";
  o.enumerated = true; o.present = true;
  o.driver_runtime_ok = true; o.cuda_init_ok = true;
  o.memory_alloc_ok = true; o.h2d_ok = true; o.kernel_exec_ok = true;
  o.sync_ok = true; o.d2h_ok = true; o.verify_ok = true;
  o.core_validation_ok = true;
  o.total_memory = o.identity.total_physical_memory;
  o.free_memory = 24ull*1024*1024*1024;
  o.used_memory = 8ull*1024*1024*1024;
  return o;
}

int main() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  Registration reg;
  reg.worker = WorkerId(10u); reg.worker_boot = WorkerBootId(100u);
  reg.protocol_version.value = 1u;
  reg.registration_generation = RegistrationGeneration(1u);
  reg.epoch = CoordinatorEpoch(1u);
  reg.os_platform = "windows";
  CHECK(store.register_worker(reg, 1000).ok());

  ObservationPolicy policy;
  policy.freshness_threshold_ms = 5000;
  auto obs = healthy_obs(WorkerBootId(100u), DeviceGeneration(1u), ObservationGeneration(1u));
  auto r = store.ingest_snapshot(WorkerId(10u), {obs}, policy, 9000);
  CHECK(r.ok());

  std::string key = canonical_device_identity(obs.identity);
  auto snap = store.snapshot();
  CHECK(snap.devices.at(key).eligibility == EligibilityState::ELIGIBLE);

  // Drain -> eligibility becomes DRAINING, state preserved.
  CHECK(store.set_drain(WorkerId(10u), key, DrainState::DRAINING, 9100).ok());
  snap = store.snapshot();
  CHECK(snap.devices.at(key).drain == DrainState::DRAINING);
  CHECK(snap.devices.at(key).eligibility == EligibilityState::DRAINING);

  // Undrain -> back to ACTIVE and ELIGIBLE.
  CHECK(store.set_drain(WorkerId(10u), key, DrainState::ACTIVE, 9200).ok());
  snap = store.snapshot();
  CHECK(snap.devices.at(key).drain == DrainState::ACTIVE);
  CHECK(snap.devices.at(key).eligibility == EligibilityState::ELIGIBLE);

  // Quarantine (independent of drain).
  QuarantineRecord q;
  q.reason = "repeated validation failure";
  q.source = "operator";
  q.generation = DeviceGeneration(1u);
  q.at = 9300;
  q.authority = CoordinatorEpoch(1u);
  CHECK(store.set_quarantine(WorkerId(10u), key, q, 9300).ok());
  snap = store.snapshot();
  CHECK(snap.devices.at(key).quarantined);
  CHECK(snap.devices.at(key).eligibility == EligibilityState::QUARANTINED);
  CHECK(snap.devices.at(key).quarantine.reason == "repeated validation failure");

  // Quarantine is independent from drain: even if we undrain, still quarantined.
  CHECK(store.set_drain(WorkerId(10u), key, DrainState::ACTIVE, 9400).ok());
  snap = store.snapshot();
  CHECK(snap.devices.at(key).quarantined);
  CHECK(snap.devices.at(key).eligibility == EligibilityState::QUARANTINED);

  // Clear quarantine -> eligible again.
  CHECK(store.clear_quarantine(WorkerId(10u), key, CoordinatorEpoch(1u), 9500).ok());
  snap = store.snapshot();
  CHECK(!snap.devices.at(key).quarantined);
  CHECK(snap.devices.at(key).eligibility == EligibilityState::ELIGIBLE);

  return tf::summary("drain_quarantine");
}
