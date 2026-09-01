#include "tests/test_fw.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/persistence/store.hpp"
#include <fstream>
#include <cstdio>

using namespace gpufleet;

int main() {
  const char* path = "test_store.gfle";
  std::remove(path);

  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  // A worker and one device.
  Registration reg;
  reg.worker = WorkerId(10u); reg.worker_boot = WorkerBootId(100u);
  reg.protocol_version.value = 1u; reg.registration_generation = RegistrationGeneration(1u);
  reg.epoch = CoordinatorEpoch(1u);
  reg.os_platform = "windows";
  reg.enumerated_devices.push_back("nvidia|NVIDIA|synthetic:0:34359738368|0000:00:00.00|synthetic|12.0|34359738368|synthetic");
  reg.supported_capabilities.push_back("cuda");
  reg.supported_capabilities.push_back("synthetic");
  CHECK(store.register_worker(reg, 1000).ok());
  DeviceObservation o;
  o.device_id = DeviceId(1u); o.observation_generation = ObservationGeneration(1u);
  o.health_generation = HealthGeneration(1u); o.observed_at = 5000;
  o.source_worker_boot = WorkerBootId(100u); o.source_worker = WorkerId(10u);
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
  ObservationPolicy policy; policy.freshness_threshold_ms = 5000;
  CHECK(store.ingest_snapshot(WorkerId(10u), {o}, policy, 9000).ok());

  // Save.
  StateSnapshot s = store.snapshot();
  CHECK(save_snapshot(path, s).ok());
  auto digest = snapshot_digest(s);

  // Load round trip.
  auto l = load_snapshot(path);
  CHECK(l.ok());
  CHECK(l.value().epoch == s.epoch);
  CHECK_EQ(l.value().workers.size(), s.workers.size());
  CHECK_EQ(l.value().devices.size(), s.devices.size());
  CHECK(snapshot_digest(l.value()) == digest);

  // Corruption: flip a byte in the body.
  {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(20); char b; f.read(&b,1); b ^= 0x55; f.seekp(20); f.write(&b,1); f.close();
    auto c = load_snapshot(path);
    CHECK(!c.ok());
    CHECK(c.error().code == Status::Corrupt);
  }
  std::remove(path);
  CHECK(save_snapshot(path, s).ok());

  // Truncation: cut the tail.
  {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::vector<char> cut(data.begin(), data.begin() + data.size() - 3);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(cut.data(), cut.size()); out.close();
    auto t = load_snapshot(path);
    CHECK(!t.ok());
    CHECK(t.error().code == Status::Truncated);
  }
  std::remove(path);
  CHECK(save_snapshot(path, s).ok());

  // Trailing garbage: append bytes.
  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << "GARBAGE";
    out.close();
    auto t = load_snapshot(path);
    CHECK(!t.ok());
    CHECK(t.error().code == Status::Truncated);  // size mismatch
  }
  std::remove(path);
  CHECK(save_snapshot(path, s).ok());

  // Bad magic: overwrite header.
  {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(0); f.write("XXXX", 4); f.close();
    auto t = load_snapshot(path);
    CHECK(!t.ok());
    CHECK(t.error().code == Status::Corrupt);
  }
  std::remove(path);

  return tf::summary("persistence");
}
