#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/observation/change.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/persistence/store.hpp"

using namespace gpufleet;

static double now_ms() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static DeviceObservation mkobs(WorkerBootId boot, WorkerId worker, ObservationGeneration og) {
  DeviceObservation o;
  o.device_id = DeviceId(1); o.observation_generation = og;
  o.health_generation = HealthGeneration(1); o.observed_at = 5000;
  o.source_worker_boot = boot; o.source_worker = worker; o.source_node = NodeId(1);
  o.epoch = CoordinatorEpoch(1); o.device_generation = DeviceGeneration(1);
  o.identity.vendor = AcceleratorVendor::Nvidia; o.identity.architecture = "blackwell";
  o.identity.compute_capability = ComputeCapability{12,0};
  o.identity.total_physical_memory = 32ull*1024*1024*1024; o.identity.driver_version.text = "13040";
  o.enumerated = true; o.present = true; o.driver_runtime_ok = true; o.cuda_init_ok = true;
  o.memory_alloc_ok = true; o.h2d_ok = true; o.kernel_exec_ok = true; o.sync_ok = true;
  o.d2h_ok = true; o.verify_ok = true; o.core_validation_ok = true;
  o.total_memory = o.identity.total_physical_memory; o.free_memory = 24ull*1024*1024*1024;
  return o;
}

int main() {
  const int N = 20000;
  FleetStateStore store(CoordinatorId(1), CoordinatorEpoch(1));

  // registration
  {
    double t0 = now_ms();
    for (int i = 0; i < N; ++i) {
      Registration reg; reg.worker = WorkerId(10); reg.worker_boot = WorkerBootId(100+i);
      reg.protocol_version.value = 1; reg.registration_generation = RegistrationGeneration(1);
      reg.epoch = CoordinatorEpoch(1);
      store.register_worker(reg, 1000);
    }
    double ms = now_ms() - t0;
    std::printf("registration            : %d ops in %.1f ms => %.0f ops/s\n", N, ms, N/(ms/1000.0));
  }
  // device snapshot ingestion
  {
    Registration reg; reg.worker = WorkerId(10); reg.worker_boot = WorkerBootId(100);
    reg.protocol_version.value = 1; reg.registration_generation = RegistrationGeneration(1);
    reg.epoch = CoordinatorEpoch(1); store.register_worker(reg, 1000);
    ObservationPolicy policy; policy.freshness_threshold_ms = 1000000;
    double t0 = now_ms();
    for (int i = 0; i < N; ++i) {
      auto o = mkobs(WorkerBootId(100), WorkerId(10), ObservationGeneration(1+i));
      store.ingest_snapshot(WorkerId(10), {o}, policy, 100000+i);
    }
    double ms = now_ms() - t0;
    std::printf("snapshot ingestion       : %d ops in %.1f ms => %.0f ops/s (%llu obs accepted)\n",
                N, ms, N/(ms/1000.0), (unsigned long long)store.accepted_observations());
  }
  // health evaluation
  {
    HealthSignals hs; hs.enumerated = true; hs.device_present = true; hs.was_known = true;
    hs.driver_runtime_ok = true; hs.cuda_init_ok = true; hs.memory_alloc_ok = true;
    hs.kernel_exec_ok = true; hs.sync_ok = true; hs.mem_roundtrip_ok = true;
    hs.last_validation_ok = true;
    double t0 = now_ms();
    volatile int sink = 0;
    for (int i = 0; i < N*10; ++i) sink += (evaluate_health(hs).state == HealthState::HEALTHY) ? 1 : 0;
    double ms = now_ms() - t0;
    std::printf("health evaluation        : %d ops in %.1f ms => %.0f ops/s\n", N*10, ms, (N*10)/(ms/1000.0));
  }
  // eligibility evaluation
  {
    EligibilityInput in; in.health = HealthState::HEALTHY; in.observation_fresh = true;
    in.cuda_available = true; in.device_present = true;
    double t0 = now_ms();
    volatile int sink = 0;
    for (int i = 0; i < N*10; ++i) sink += (evaluate_eligibility(in).state == EligibilityState::ELIGIBLE) ? 1 : 0;
    double ms = now_ms() - t0;
    std::printf("eligibility evaluation   : %d ops in %.1f ms => %.0f ops/s\n", N*10, ms, (N*10)/(ms/1000.0));
  }
  // capability normalization
  {
    double t0 = now_ms();
    std::vector<Capability> caps;
    volatile std::size_t sink = 0;
    for (int i = 0; i < N; ++i) {
      caps.clear();
      for (int k = 0; k < 16; ++k) { Capability c; c.name = "cap" + std::to_string(k); c.kind = CapabilityKind::DISCOVERED; caps.push_back(std::move(c)); }
      sink += caps.size();
    }
    double ms = now_ms() - t0;
    std::printf("capability normalization: %d sets in %.1f ms => %.0f ops/s\n", N, ms, N/(ms/1000.0));
  }
  // indexed device lookup
  {
    std::map<std::string, DeviceState> devmap;
    for (int i = 0; i < 1000; ++i) devmap["dev" + std::to_string(i)] = DeviceState{};
    std::vector<std::string> keys;
    for (int i = 0; i < 1000; ++i) keys.push_back("dev" + std::to_string(i));
    double t0 = now_ms();
    volatile std::size_t sink = 0;
    for (int i = 0; i < N; ++i) sink += devmap.count(keys[i % 1000]);
    double ms = now_ms() - t0;
    std::printf("indexed device lookup    : %d ops in %.1f ms => %.0f ops/s (1000 devices)\n", N, ms, N/(ms/1000.0));
  }
  // snapshot serialization
  {
    StateSnapshot s = store.snapshot();
    double t0 = now_ms();
    volatile std::size_t sink = 0;
    for (int i = 0; i < N/10; ++i) { auto b = encode_snapshot(s); sink += b.size(); }
    double ms = now_ms() - t0;
    std::printf("snapshot serialization   : %d ops in %.1f ms => %.0f ops/s (snapshot=%d bytes)\n", N/10, ms, (N/10)/(ms/1000.0), (int)encode_snapshot(s).size());
  }
  // recovery (restore)
  {
    StateSnapshot s = store.snapshot();
    FleetStateStore store2(CoordinatorId(1), CoordinatorEpoch(1));
    double t0 = now_ms();
    for (int i = 0; i < N/10; ++i) store2.restore(s);
    double ms = now_ms() - t0;
    std::printf("recovery (restore)       : %d ops in %.1f ms => %.0f ops/s\n", N/10, ms, (N/10)/(ms/1000.0));
  }
  // state-diff generation
  {
    DeviceState a, b; a.present = true; b.present = true;
    a.health = HealthState::HEALTHY; b.health = HealthState::DEGRADED;
    b.eligibility = EligibilityState::DEGRADED_ELIGIBLE;
    double t0 = now_ms();
    volatile std::size_t sink = 0;
    for (int i = 0; i < N; ++i) { auto ch = diff_device_state(a, b, 1, DeviceGeneration(1), CoordinatorEpoch(1)); sink += ch.size(); }
    double ms = now_ms() - t0;
    std::printf("state-diff generation    : %d ops in %.1f ms => %.0f ops/s\n", N, ms, N/(ms/1000.0));
  }
  // protocol encode/decode
  {
    Message m; m.type = MessageType::HEARTBEAT; m.worker_boot = WorkerBootId(99);
    auto b = encode_message(m);
    double t0 = now_ms();
    volatile std::size_t sink = 0;
    for (int i = 0; i < N; ++i) { auto f = encode_frame(m.type, b); sink += f.size(); auto d = decode_message(std::span<const std::uint8_t>(b.data(), b.size())); sink += d.ok() ? 1 : 0; }
    double ms = now_ms() - t0;
    std::printf("protocol encode+decode   : %d ops in %.1f ms => %.0f ops/s\n", N, ms, N/(ms/1000.0));
  }
  // concurrent observation ingestion
  {
    FleetStateStore cstore(CoordinatorId(1), CoordinatorEpoch(1));
    Registration reg; reg.worker = WorkerId(10); reg.worker_boot = WorkerBootId(100);
    reg.protocol_version.value = 1; reg.registration_generation = RegistrationGeneration(1);
    reg.epoch = CoordinatorEpoch(1); cstore.register_worker(reg, 1000);
    ObservationPolicy policy; policy.freshness_threshold_ms = 1000000;
    const int TH = 8;
    double t0 = now_ms();
    std::vector<std::thread> thr;
    std::atomic<std::uint64_t> total{0};
    for (int t = 0; t < TH; ++t)
      thr.emplace_back([&]{
        std::uint64_t n = 0;
        for (int i = 0; i < 1000; ++i) {
          auto o = mkobs(WorkerBootId(100), WorkerId(10), ObservationGeneration(1+i));
          cstore.ingest_snapshot(WorkerId(10), {o}, policy, 100000+i);
          n++;
        }
        total += n;
      });
    for (auto& th : thr) th.join();
    double ms = now_ms() - t0;
    std::printf("concurrent ingestion     : %d ops in %.1f ms => %.0f ops/s (%d threads)\n", (int)total.load(), ms, total.load()/(ms/1000.0), TH);
  }
  return 0;
}
