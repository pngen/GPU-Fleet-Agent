#include "tests/test_fw.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"

using namespace gpufleet;

static DeviceObservation mk(WorkerBootId boot, ObservationGeneration og) {
  DeviceObservation o;
  o.device_id = DeviceId(1u); o.observation_generation = og;
  o.health_generation = HealthGeneration(1u); o.observed_at = 5000;
  o.source_worker_boot = boot; o.source_worker = WorkerId(10u);
  o.source_node = NodeId(1u); o.epoch = CoordinatorEpoch(1u);
  o.device_generation = DeviceGeneration(1u);
  o.identity.vendor = AcceleratorVendor::Nvidia; o.identity.vendor_name = "NVIDIA";
  o.identity.architecture = "blackwell";
  o.identity.compute_capability = ComputeCapability{12,0};
  o.identity.total_physical_memory = 32ull*1024*1024*1024;
  o.identity.driver_version.text = "13040";
  o.enumerated = true; o.present = true; o.driver_runtime_ok = true; o.cuda_init_ok = true;
  o.memory_alloc_ok = true; o.h2d_ok = true; o.kernel_exec_ok = true; o.sync_ok = true;
  o.d2h_ok = true; o.verify_ok = true; o.core_validation_ok = true;
  o.total_memory = o.identity.total_physical_memory; o.free_memory = 24ull*1024*1024*1024;
  return o;
}

int main() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  Registration reg;
  reg.worker = WorkerId(10u); reg.worker_boot = WorkerBootId(100u);
  reg.protocol_version.value = 1u; reg.registration_generation = RegistrationGeneration(1u);
  reg.epoch = CoordinatorEpoch(1u);
  CHECK(store.register_worker(reg, 1000).ok());
  ObservationPolicy policy; policy.freshness_threshold_ms = 5000;
  CHECK(store.ingest_snapshot(WorkerId(10u), {mk(WorkerBootId(100u), ObservationGeneration(1u))}, policy, 9000).ok());

  auto snap = store.snapshot();
  auto blob = encode_snapshot(snap);
  auto dec = decode_snapshot(blob);
  CHECK(dec.ok());

  // Recover into a fresh store: recovered observations must NOT be fresh.
  FleetStateStore store2(CoordinatorId(1u), CoordinatorEpoch(1u));
  CHECK(store2.restore(dec.value()).ok());
  auto s2 = store2.snapshot();
  CHECK_EQ(s2.workers.size(), 1u);
  bool any_fresh = false; bool any_eligible = false;
  for (auto& [k, d] : s2.devices) {
    if (d.observation_fresh) any_fresh = true;
    if (d.eligibility == EligibilityState::ELIGIBLE) any_eligible = true;
  }
  CHECK(!any_fresh);
  CHECK(!any_eligible);
  // The recovered device should be STALE (dynamic observation), not ELIGIBLE.
  for (auto& [k, d] : s2.devices) CHECK(d.eligibility == EligibilityState::STALE);

  return tf::summary("recovery");
}
