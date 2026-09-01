#include "tests/test_fw.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include <thread>
#include <atomic>
#include <vector>

using namespace gpufleet;

static DeviceObservation obs_for(WorkerBootId boot, WorkerId worker, DeviceGeneration dg, ObservationGeneration og) {
  DeviceObservation o;
  o.device_id = DeviceId(1u);
  o.observation_generation = og;
  o.health_generation = HealthGeneration(1u);
  o.observed_at = 5000;
  o.source_worker_boot = boot;
  o.source_worker = worker;
  o.source_node = NodeId(1u);
  o.epoch = CoordinatorEpoch(1u);
  o.device_generation = dg;
  o.identity.vendor = AcceleratorVendor::Nvidia;
  o.identity.vendor_name = "NVIDIA";
  o.identity.uuid.value = "GPU-w" + std::to_string(worker.value());  // distinct device per worker
  o.identity.architecture = "blackwell";
  o.identity.compute_capability = ComputeCapability{12,0};
  o.identity.total_physical_memory = 32ull*1024*1024*1024;
  o.identity.driver_version.text = "13040";
  o.enumerated = true; o.present = true;
  o.driver_runtime_ok = true; o.cuda_init_ok = true;
  o.memory_alloc_ok = true; o.h2d_ok = true; o.kernel_exec_ok = true;
  o.sync_ok = true; o.d2h_ok = true; o.verify_ok = true; o.core_validation_ok = true;
  o.total_memory = o.identity.total_physical_memory;
  o.free_memory = 24ull*1024*1024*1024;
  o.used_memory = 8ull*1024*1024*1024;
  return o;
}

int main() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  for (int i = 0; i < 5; ++i) {
    Registration reg;
    reg.worker = WorkerId(10u + i); reg.worker_boot = WorkerBootId(100u + i);
    reg.protocol_version.value = 1u;
    reg.registration_generation = RegistrationGeneration(1u);
    reg.epoch = CoordinatorEpoch(1u);
    CHECK(store.register_worker(reg, 1000).ok());
  }
  ObservationPolicy policy;
  policy.freshness_threshold_ms = 5000;
  for (int i = 0; i < 3; ++i) {
    auto o = obs_for(WorkerBootId(100u + i), WorkerId(10u + i), DeviceGeneration(1u), ObservationGeneration(1u));
    CHECK(store.ingest_snapshot(WorkerId(10u + i), {o}, policy, 9000).ok());
  }
  auto a = store.accounting();
  CHECK_EQ(a.registered_workers, 5u);
  CHECK_EQ(a.live_workers, 5u);
  CHECK_EQ(a.active_registrations, 5u);
  CHECK_EQ(a.devices, 3u);
  CHECK_EQ(a.eligible_devices, 3u);
  CHECK(a.is_valid());

  // Mark a worker lost -> accounting shifts.
  CHECK(store.mark_worker_lost(WorkerId(10u), 9500).ok());
  a = store.accounting();
  CHECK_EQ(a.live_workers, 4u);
  CHECK_EQ(a.stale_workers, 1u);
  CHECK(a.is_valid());

  // Concurrent mutation + reads must be safe and deterministic.
  std::atomic<bool> done{false};
  std::vector<std::thread> writers;
  for (int t = 0; t < 4; ++t) {
    writers.emplace_back([&, t]() {
      for (int i = 0; i < 200; ++i) {
        auto o = obs_for(WorkerBootId(100u + t), WorkerId(10u + t), DeviceGeneration(1u), ObservationGeneration(1u + i));
        store.ingest_snapshot(WorkerId(10u + t), {o}, policy, 9000 + i);
      }
    });
  }
  std::thread reader([&]() {
    while (!done.load()) { auto s = store.snapshot(); (void)s; auto ac = store.accounting(); (void)ac; }
  });
  for (auto& w : writers) w.join();
  done.store(true);
  reader.join();
  a = store.accounting();
  CHECK(a.is_valid());
  CHECK_EQ(a.registered_workers, 5u);
  // concurrent observations were accepted (at least the initial 3) and accounting
  // stays consistent with canonical state.
  CHECK(a.accepted_observations >= 3u);
  CHECK(a.devices >= 3u);
  return tf::summary("fleet_state");
}
