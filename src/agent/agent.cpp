#include "gpufleetagent/agent/agent.hpp"

#include <chrono>
#include <cstdio>
#include <windows.h>  // GetCurrentProcessId
#undef ERROR  // windows.h defines ERROR=0, which collides with MessageType::ERROR.
#undef min
#undef max

#include "gpufleetagent/cuda/cuda_backend.hpp"

#include "gpufleetagent/transport/transport.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/observation_codec.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/device/synthetic_backend.hpp"

namespace gpufleet {

namespace {
std::uint64_t make_fresh_boot_id() {
  // A fresh WorkerBootId for each process incarnation. Derive from a counter,
  // the current epoch time, and the process id so it is unique per launch.
  static std::atomic<std::uint64_t> counter{0};
  auto t = static_cast<std::uint64_t>(now_millis());
  auto pid = static_cast<std::uint64_t>(GetCurrentProcessId());
  return (t << 16) ^ (pid << 8) ^ (counter.fetch_add(1) & 0xFF);
}
}  // namespace

WorkerAgent::WorkerAgent(Options opts) : opts_(std::move(opts)) {
  if (opts_.boot.is_zero()) opts_.boot = WorkerBootId(make_fresh_boot_id());
  if (opts_.backend == "cuda") {
    backend_ = make_cuda_backend();
    if (!backend_) backend_ = std::make_unique<SyntheticDeviceBackend>();
  } else {
    backend_ = std::make_unique<SyntheticDeviceBackend>();
  }
}

WorkerAgent::~WorkerAgent() { stop(); }

void WorkerAgent::stop() { running_.store(false); }

Result<void> WorkerAgent::run() {
  running_.store(true);
  reconnect_loop();
  return ok_result();
}

void WorkerAgent::reconnect_loop() {
  while (running_.load()) {
    auto conn = TcpStream::connect(opts_.coordinator_host, opts_.coordinator_port, 2000);
    if (!conn.ok()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(opts_.reconnect_backoff_ms));
      continue;
    }
    TcpStream stream = conn.move_value();
    stream.set_timeout(200);

    bool registered = connect_and_register(stream).ok();

    FrameDecoder dec;
    std::vector<std::uint8_t> buf(64 * 1024);
    Frame pending;
    std::string perr;

    if (!registered) {
      stream.close();
      std::this_thread::sleep_for(std::chrono::milliseconds(opts_.reconnect_backoff_ms));
      continue;
    }

    while (running_.load()) {
      // Publish current device/capability observations.
      publish_snapshot(stream);

      // Drain any inbound frames (ACKs / admin) without blocking.
      bool alive = true;
      for (int i = 0; i < 16; ++i) {
        auto recv = stream.recv_some_opt(buf);
        if (!recv.ok()) { alive = false; break; }
        if (!recv.value().has_value()) break;
        std::size_t n = *recv.value();
        if (n == 0) { alive = false; break; }
        auto res = dec.feed(std::span<const std::uint8_t>(buf.data(), n), pending, perr);
        while (res == FrameDecoder::Result::FrameReady) {
          auto dm = decode_message(pending.payload);
          if (dm.ok()) handle_admin(stream, dm.value());
          res = dec.feed(std::span<const std::uint8_t>(), pending, perr);
        }
        if (res == FrameDecoder::Result::Error) { alive = false; break; }
      }
      if (!alive) break;

      send_heartbeat(stream);
      std::this_thread::sleep_for(std::chrono::milliseconds(opts_.heartbeat_interval_ms));
    }

    stream.close();
    if (!running_.load()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(opts_.reconnect_backoff_ms));
  }
}

Result<void> WorkerAgent::connect_and_register(TcpStream& stream) {
  // HELLO to learn the coordinator epoch.
  Message hello;
  hello.type = MessageType::HELLO;
  hello.worker = opts_.worker;
  hello.worker_boot = opts_.boot;
  auto hb = encode_message(hello);
  auto hf = encode_frame(hello.type, hb);
  auto s = stream.send_all(hf);
  if (!s.ok()) return error_result(Status::Io, "hello send failed");

  Message ack;
  auto r = read_one_message(stream, ack);
  if (!r.ok()) return error_result(Status::Io, "no hello ack");
  last_known_epoch_ = ack.epoch;

  // Build registration with the learned epoch.
  Registration reg;
  reg.node = opts_.node;
  reg.worker = opts_.worker;
  reg.worker_boot = opts_.boot;
  reg.agent_version.version = SemanticVersion{1, 0, 0};
  reg.protocol_version.value = protocol_version();
  reg.os_platform = opts_.os_platform;
  reg.registration_generation = reg_gen_;
  reg.epoch = last_known_epoch_;
  auto ev = backend_->enumerate();
  if (ev.ok()) {
    for (auto& d : ev.value()) {
      reg.enumerated_devices.push_back(canonical_device_identity(d.identity));
    }
  }
  reg.supported_capabilities = {"cuda", "synthetic"};

  Message rmsg;
  rmsg.type = MessageType::REGISTER;
  rmsg.worker = opts_.worker;
  rmsg.worker_boot = opts_.boot;
  rmsg.epoch = last_known_epoch_;
  rmsg.payload = encode_registration(reg);
  auto rb = encode_message(rmsg);
  auto rf = encode_frame(rmsg.type, rb);
  s = stream.send_all(rf);
  if (!s.ok()) return error_result(Status::Io, "register send failed");

  Message ack2;
  r = read_one_message(stream, ack2);
  if (!r.ok()) return error_result(Status::Io, "no register ack");
  if (!ack2.ok) return error_result(Status::Rejected, "registration rejected: " + ack2.reason);
  reg_gen_ = reg_gen_.next();
  return ok_result();
}

Result<void> WorkerAgent::read_one_message(TcpStream& stream, Message& msg) {
  FrameDecoder dec;
  std::vector<std::uint8_t> buf(64 * 1024);
  Frame pending;
  std::string perr;
  for (int i = 0; i < 64; ++i) {
    auto recv = stream.recv_some_opt(buf);
    if (!recv.ok()) return error_result(Status::Io, "recv error waiting for message");
    if (!recv.value().has_value()) continue;
    std::size_t n = *recv.value();
    if (n == 0) return error_result(Status::Io, "connection closed waiting for message");
    auto res = dec.feed(std::span<const std::uint8_t>(buf.data(), n), pending, perr);
    while (res == FrameDecoder::Result::FrameReady) {
      auto dm = decode_message(pending.payload);
      if (dm.ok()) { msg = dm.move_value(); return ok_result(); }
      res = dec.feed(std::span<const std::uint8_t>(), pending, perr);
    }
    if (res == FrameDecoder::Result::Error) return error_result(Status::Io, "frame error waiting for message");
  }
  return error_result(Status::Io, "timeout waiting for message");
}

void WorkerAgent::handle_admin(TcpStream& stream, const Message& msg) {
  if (msg.type == MessageType::ACK || msg.type == MessageType::ERROR ||
      msg.type == MessageType::REGISTER_ACK || msg.type == MessageType::SNAPSHOT_RESPONSE) {
    return;
  }
  Message ack;
  ack.type = MessageType::ACK;
  ack.ok = true;
  ack.request_id = msg.request_id;
  ack.worker_boot = opts_.boot;
  ack.worker = opts_.worker;
  auto body = encode_message(ack);
  auto frame = encode_frame(ack.type, body);
  stream.send_all(frame);
}

Result<void> WorkerAgent::publish_snapshot(TcpStream& stream) {
  auto ev = backend_->enumerate();
  if (!ev.ok()) return ok_result();
  std::vector<DeviceObservation> batch;
  for (auto& d : ev.value()) {
    auto probe = backend_->probe(d);
    if (!probe.ok()) continue;
    ObservationMetadata meta;
    meta.device_id = DeviceId(d.ordinal + 1);
    meta.observation_generation = obs_gen_;
    meta.health_generation = health_gen_;
    meta.observed_at = now_millis();
    meta.source_worker_boot = opts_.boot;
    meta.source_worker = opts_.worker;
    meta.source_node = opts_.node;
    meta.epoch = last_known_epoch_;
    meta.device_generation = device_gen_;
    DeviceObservation o = normalize_observation(probe.value(), meta);
    o.epoch = last_known_epoch_;
    batch.push_back(std::move(o));
  }
  if (batch.empty()) return ok_result();

  Message msg;
  msg.type = MessageType::DEVICE_SNAPSHOT;
  msg.worker = opts_.worker;
  msg.worker_boot = opts_.boot;
  msg.epoch = last_known_epoch_;
  msg.payload = encode_observation_batch(batch);
  auto body = encode_message(msg);
  auto frame = encode_frame(msg.type, body);
  stream.send_all(frame);
  obs_gen_ = obs_gen_.next();
  device_gen_ = device_gen_.next();
  return ok_result();
}

Result<void> WorkerAgent::send_heartbeat(TcpStream& stream) {
  Message msg;
  msg.type = MessageType::HEARTBEAT;
  msg.worker = opts_.worker;
  msg.worker_boot = opts_.boot;
  msg.epoch = last_known_epoch_;
  msg.health_gen = health_gen_;
  auto body = encode_message(msg);
  auto frame = encode_frame(msg.type, body);
  return stream.send_all(frame);
}

DeviceObservation WorkerAgent::to_observation(const EnumeratedDevice& dev, const DeviceProbe& probe,
                                              const ObservationMetadata& meta) {
  (void)dev;
  return normalize_observation(probe, meta);
}

}  // namespace gpufleet
