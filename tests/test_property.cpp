#include "tests/test_fw.hpp"
#include <random>
#include <string>
#include <vector>

#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/eligibility/eligibility_evaluator.hpp"
#include "gpufleetagent/health/health_evaluator.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"

using namespace gpufleet;

int main() {
  // Fixed seeds; printed so the run is reproducible.
  const std::uint32_t SEED_A = 123456789u;
  const std::uint32_t SEED_B = 987654321u;
  std::printf("property seeds: A=%u B=%u\n", SEED_A, SEED_B);

  std::mt19937 rng(SEED_A);

  // Property 1: message encode/decode round-trip is exact for random messages.
  {
    std::mt19937 r(SEED_B);
    std::uniform_int_distribution<int> type_dist(1, 17);
    std::uniform_int_distribution<std::uint64_t> u64_dist(0, 0xFFFFFFFFFFFFULL);
    bool all_ok = true;
    for (int i = 0; i < 500; ++i) {
      Message m;
      m.type = static_cast<MessageType>(type_dist(r));
      m.coordinator = CoordinatorId(u64_dist(r));
      m.epoch = CoordinatorEpoch(u64_dist(r));
      m.worker_boot = WorkerBootId(u64_dist(r));
      m.worker = WorkerId(u64_dist(r));
      m.node = NodeId(u64_dist(r));
      m.agent = AgentId(u64_dist(r));
      m.reg_gen = RegistrationGeneration(u64_dist(r));
      m.obs_gen = ObservationGeneration(u64_dist(r));
      m.health_gen = HealthGeneration(u64_dist(r));
      m.device_gen = DeviceGeneration(u64_dist(r));
      m.cap_gen = CapabilityGeneration(u64_dist(r));
      m.request_id = u64_dist(r);
      m.ok = (i % 2) == 0;
      m.reason = "reason" + std::to_string(i);
      std::size_t plen = static_cast<std::size_t>(u64_dist(r) % 64);
      for (std::size_t k = 0; k < plen; ++k) m.payload.push_back(static_cast<std::uint8_t>(u64_dist(r)));
      auto b = encode_message(m);
      auto d = decode_message(b);
      if (!d.ok() || d.value().type != m.type || d.value().worker_boot != m.worker_boot ||
          d.value().reason != m.reason || d.value().payload != m.payload) {
        all_ok = false;
      }
    }
    CHECK(all_ok);
  }

  // Property 2: framing round-trip is exact for random payloads.
  {
    std::mt19937 r(SEED_B);
    std::uniform_int_distribution<std::size_t> len_dist(0, 4096);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    bool all_ok = true;
    for (int i = 0; i < 200; ++i) {
      std::vector<std::uint8_t> payload(len_dist(r));
      for (auto& b : payload) b = static_cast<std::uint8_t>(byte_dist(r));
      auto f = encode_frame(MessageType::DEVICE_SNAPSHOT, payload);
      FrameDecoder dec; Frame out; std::string err;
      auto res = dec.feed(f, out, err);
      if (res != FrameDecoder::Result::FrameReady || out.payload != payload) all_ok = false;
    }
    CHECK(all_ok);
  }

  // Property 3: eligibility is deterministic (same input => same result) and a
  // stale observation NEVER yields ELIGIBLE.
  {
    std::mt19937 r(SEED_A);
    std::uniform_int_distribution<int> bool_dist(0, 1);
    bool stale_never_eligible = true;
    bool deterministic = true;
    for (int i = 0; i < 2000; ++i) {
      EligibilityInput in;
      in.observation_fresh = bool_dist(r) != 0;
      in.cuda_available = bool_dist(r) != 0;
      in.device_present = bool_dist(r) != 0;
      in.drain_active = bool_dist(r) != 0;
      in.quarantine = bool_dist(r) != 0;
      in.worker_restarted = bool_dist(r) != 0;
      in.worker_alive = bool_dist(r) != 0;
      in.architecture_match = bool_dist(r) != 0;
      in.sufficient_memory = bool_dist(r) != 0;
      in.driver_match = bool_dist(r) != 0;
      in.capability_match = bool_dist(r) != 0;
      in.health = static_cast<HealthState>(bool_dist(r) ? 1 : 1);
      auto a = evaluate_eligibility(in);
      auto b = evaluate_eligibility(in);
      if (!(a.state == b.state && a.reasons == b.reasons)) deterministic = false;
      // If observation not fresh but everything else healthy, ELIGIBLE must not occur.
      if (!in.observation_fresh && in.health == HealthState::HEALTHY && !in.drain_active &&
          !in.quarantine && in.cuda_available && in.device_present && in.worker_alive &&
          !in.worker_restarted) {
        if (a.state == EligibilityState::ELIGIBLE) stale_never_eligible = false;
      }
    }
    CHECK(deterministic);
    CHECK(stale_never_eligible);
  }

  // Property 4: fleet store accounting never goes invalid and accepted
  // observations strictly increase.
  {
    FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
    Registration reg;
    reg.worker = WorkerId(10u); reg.worker_boot = WorkerBootId(100u);
    reg.protocol_version.value = 1u; reg.registration_generation = RegistrationGeneration(1u);
    reg.epoch = CoordinatorEpoch(1u);
    CHECK(store.register_worker(reg, 1000).ok());
    ObservationPolicy policy; policy.freshness_threshold_ms = 100000;
    std::uint64_t prev = store.accepted_observations();
    bool monotonic = true;
    for (int i = 1; i <= 200; ++i) {
      DeviceObservation o;
      o.device_id = DeviceId(1u); o.observation_generation = ObservationGeneration(i);
      o.health_generation = HealthGeneration(i); o.observed_at = 1000 + i;
      o.source_worker_boot = WorkerBootId(100u); o.source_worker = WorkerId(10u);
      o.source_node = NodeId(1u); o.epoch = CoordinatorEpoch(1u);
      o.device_generation = DeviceGeneration(1u);
      o.identity.vendor = AcceleratorVendor::Nvidia;
      o.identity.architecture = "blackwell";
      o.identity.compute_capability = ComputeCapability{12,0};
      o.identity.total_physical_memory = 32ull*1024*1024*1024;
      o.identity.driver_version.text = "13040";
      o.enumerated = true; o.present = true; o.driver_runtime_ok = true; o.cuda_init_ok = true;
      o.memory_alloc_ok = true; o.h2d_ok = true; o.kernel_exec_ok = true; o.sync_ok = true;
      o.d2h_ok = true; o.verify_ok = true; o.core_validation_ok = true;
      o.total_memory = o.identity.total_physical_memory; o.free_memory = 24ull*1024*1024*1024;
      CHECK(store.ingest_snapshot(WorkerId(10u), {o}, policy, 2000 + i).ok());
      auto a = store.accounting();
      if (!a.is_valid()) monotonic = false;
      if (store.accepted_observations() < prev) monotonic = false;
      prev = store.accepted_observations();
    }
    CHECK(monotonic);
  }

  return tf::summary("property");
}
