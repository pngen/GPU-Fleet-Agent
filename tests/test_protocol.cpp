#include "tests/test_fw.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/observation/observation.hpp"

using namespace gpufleet;

static DeviceObservation mkobs(WorkerBootId boot, WorkerId worker, ObservationGeneration og, CoordinatorEpoch epoch) {
  DeviceObservation o;
  o.device_id = DeviceId(1u); o.observation_generation = og;
  o.health_generation = HealthGeneration(1u);
  o.observed_at = 5000; o.source_worker_boot = boot; o.source_worker = worker;
  o.source_node = NodeId(1u); o.epoch = epoch; o.device_generation = DeviceGeneration(1u);
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
  // Message codec round trip.
  for (std::uint8_t t = static_cast<std::uint8_t>(MessageType::HELLO);
       t <= static_cast<std::uint8_t>(MessageType::ERROR); ++t) {
    if (!valid_message_type(t)) continue;
    Message m; m.type = static_cast<MessageType>(t);
    m.epoch = CoordinatorEpoch(2u); m.worker_boot = WorkerBootId(9u);
    m.worker = WorkerId(3u); m.node = NodeId(4u); m.agent = AgentId(5u);
    m.reg_gen = RegistrationGeneration(6u); m.obs_gen = ObservationGeneration(7u);
    m.health_gen = HealthGeneration(8u); m.device_gen = DeviceGeneration(9u);
    m.cap_gen = CapabilityGeneration(10u); m.request_id = 11u; m.ok = true;
    m.reason = "ok"; m.payload = {1,2,3,4};
    auto b = encode_message(m);
    auto d = decode_message(b);
    CHECK(d.ok());
    CHECK(d.value().type == m.type);
    CHECK(d.value().worker_boot == m.worker_boot);
    CHECK(d.value().reason == m.reason);
    CHECK(d.value().payload == m.payload);
  }

  // Frame codec round trip with various payloads.
  {
    auto f = encode_frame(MessageType::HEARTBEAT, std::span<const std::uint8_t>({1,2,3}));
    FrameDecoder dec; Frame out; std::string err;
    auto res = dec.feed(f, out, err);
    CHECK(res == FrameDecoder::Result::FrameReady);
    CHECK(out.type == MessageType::HEARTBEAT);
    CHECK(out.payload.size() == 3u);
    CHECK(dec.buffered() == 0u);
  }

  // Partial feeds (half-written frame) repeatedly => NeedMore then Ready.
  {
    auto f = encode_frame(MessageType::ACK, std::span<const std::uint8_t>({7,8,9,10}));
    FrameDecoder dec; Frame out; std::string err;
    auto r1 = dec.feed(std::span<const std::uint8_t>(f.data(), 3), out, err);
    CHECK(r1 == FrameDecoder::Result::NeedMore);
    auto r2 = dec.feed(std::span<const std::uint8_t>(f.data()+3, f.size()-3), out, err);
    CHECK(r2 == FrameDecoder::Result::FrameReady);
    CHECK(out.type == MessageType::ACK);
    CHECK(out.payload == std::vector<std::uint8_t>({7,8,9,10}));
  }

  // Rejections.
  {
    // Bad magic.
    auto f = encode_frame(MessageType::HELLO, {});
    f[0] = 0x00;
    FrameDecoder dec; Frame out; std::string err;
    CHECK(dec.feed(f, out, err) == FrameDecoder::Result::Error);
  }
  {
    // Unknown protocol version.
    auto f = encode_frame(MessageType::HELLO, {});
    f[4] = 0xFF; f[5] = 0xFF;
    FrameDecoder dec; Frame out; std::string err;
    CHECK(dec.feed(f, out, err) == FrameDecoder::Result::Error);
  }
  {
    // Invalid enum message type.
    auto f = encode_frame(MessageType::HELLO, {});
    f[6] = 200;  // not a valid MessageType
    FrameDecoder dec; Frame out; std::string err;
    CHECK(dec.feed(f, out, err) == FrameDecoder::Result::Error);
  }
  {
    // CRC mismatch.
    auto f = encode_frame(MessageType::HELLO, {});
    f[f.size()-1] ^= 0xFF;
    FrameDecoder dec; Frame out; std::string err;
    CHECK(dec.feed(f, out, err) == FrameDecoder::Result::Error);
  }
  {
    // Oversized payload: craft a frame with a huge payload length.
    std::vector<std::uint8_t> hdr(15, 0);
    hdr[0]='G'; hdr[1]='F'; hdr[2]='L'; hdr[3]='A';
    hdr[4]=0; hdr[5]=1; hdr[6]=1;
    // length = 0x7FFFFFFF
    hdr[7]=0x7F; hdr[8]=0xFF; hdr[9]=0xFF; hdr[10]=0xFF;
    FrameDecoder dec; Frame out; std::string err;
    CHECK(dec.feed(hdr, out, err) == FrameDecoder::Result::Error);
  }
  {
    // Trailing garbage after a valid message: decode_message rejects.
    auto b = encode_message(Message{});
    b.push_back(0xAA);
    auto d = decode_message(b);
    CHECK(!d.ok());
    CHECK(d.error().code == Status::Malformed);
  }
  {
    // Truncated message decode.
    auto b = encode_message(Message{});
    std::vector<std::uint8_t> cut(b.begin(), b.begin() + b.size() - 1);
    auto d = decode_message(cut);
    CHECK(!d.ok());
  }

  // Stale authority rejection at the store.
  {
    FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
    Registration reg;
    reg.worker = WorkerId(10u); reg.worker_boot = WorkerBootId(100u);
    reg.protocol_version.value = 1u;
    reg.registration_generation = RegistrationGeneration(1u);
    reg.epoch = CoordinatorEpoch(1u);
    CHECK(store.register_worker(reg, 1000).ok());

    // Stale epoch registration rejected.
    Registration reg2 = reg; reg2.epoch = CoordinatorEpoch(0u);
    CHECK(store.register_worker(reg2, 1000).error().code == Status::Stale);

    // Stale epoch observation rejected.
    ObservationPolicy policy;
    auto o = mkobs(WorkerBootId(100u), WorkerId(10u), ObservationGeneration(1u), CoordinatorEpoch(0u));
    auto r = store.ingest_snapshot(WorkerId(10u), {o}, policy, 9000);
    CHECK(!r.ok());
    CHECK(r.error().code == Status::Stale);

    // Stale WorkerBootId observation rejected.
    auto o2 = mkobs(WorkerBootId(999u), WorkerId(10u), ObservationGeneration(1u), CoordinatorEpoch(1u));
    auto r2 = store.ingest_snapshot(WorkerId(10u), {o2}, policy, 9000);
    CHECK(!r2.ok());
    CHECK(r2.error().code == Status::Stale);

    // Stale heartbeat boot rejected.
    CHECK(store.heartbeat(WorkerId(10u), WorkerBootId(999u), CoordinatorEpoch(1u), 9000, HealthGeneration(1u)).error().code == Status::Stale);
  }
  return tf::summary("protocol");
}
