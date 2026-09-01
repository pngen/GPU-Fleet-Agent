#pragma once
// Deterministic message serialization for the control protocol.
//
// A Message is encoded to a byte payload (the frame payload). Encoding is
// big-endian and structural. Decoding rejects: unknown message types, invalid
// enums, trailing garbage, and bounded-length violations. Authority/staleness
// semantics (stale epoch, stale WorkerBootId, stale generation) are enforced by
// the accepting state machine, not by the codec.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

namespace gpufleet {

inline std::vector<std::uint8_t> encode_message(const Message& m) {
  ByteWriter w;
  w.u8(static_cast<std::uint8_t>(m.type));
  w.u64(m.coordinator.value());
  w.u64(m.epoch.value());
  w.u64(m.worker_boot.value());
  w.u64(m.worker.value());
  w.u64(m.node.value());
  w.u64(m.agent.value());
  w.u64(m.reg_gen.value());
  w.u64(m.obs_gen.value());
  w.u64(m.health_gen.value());
  w.u64(m.device_gen.value());
  w.u64(m.cap_gen.value());
  w.u64(m.request_id);
  w.u8(m.ok ? 1 : 0);
  w.string(m.reason);
  w.bytes(m.payload);
  return w.take();
}

inline Result<Message> decode_message(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  std::uint8_t type = 0;
  if (!r.u8(type)) return error_result(Status::Truncated, "message type truncated");
  if (!valid_message_type(type)) return error_result(Status::InvalidEnum, "unknown message type");

  Message m;
  m.type = static_cast<MessageType>(type);

  std::uint64_t v;

  if (!r.u64(v)) return error_result(Status::Truncated, "coordinator truncated");
  m.coordinator = CoordinatorId(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "epoch truncated");
  m.epoch = CoordinatorEpoch(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "worker_boot truncated");
  m.worker_boot = WorkerBootId(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "worker truncated");
  m.worker = WorkerId(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "node truncated");
  m.node = NodeId(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "agent truncated");
  m.agent = AgentId(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "reg_gen truncated");
  m.reg_gen = RegistrationGeneration(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "obs_gen truncated");
  m.obs_gen = ObservationGeneration(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "health_gen truncated");
  m.health_gen = HealthGeneration(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "device_gen truncated");
  m.device_gen = DeviceGeneration(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "cap_gen truncated");
  m.cap_gen = CapabilityGeneration(v);
  if (!r.u64(v)) return error_result(Status::Truncated, "request_id truncated");
  m.request_id = v;

  std::uint8_t okb = 0;
  if (!r.u8(okb)) return error_result(Status::Truncated, "ok truncated");
  m.ok = (okb != 0);

  auto reason = r.string(4096);
  if (!reason.ok()) return error_result(Status::Truncated, "reason truncated");
  m.reason = reason.move_value();

  auto payload = r.bytes();
  if (!payload.ok()) return error_result(Status::Truncated, "payload truncated");
  m.payload.assign(payload.value().begin(), payload.value().end());

  if (!r.done()) return error_result(Status::Malformed, "trailing garbage after message");
  return ok_result(std::move(m));
}

}  // namespace gpufleet
