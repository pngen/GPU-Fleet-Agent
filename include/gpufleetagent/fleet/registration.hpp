#pragma once
// Worker registration model.
//
// Registration carries everything a coordinator needs to admit a worker
// incarnation as authoritative: the identity of the node/worker, the
// incarnation (WorkerBootId), the software and protocol versions, the
// platform/driver/runtime identities, the enumerated devices, and the
// supported capabilities — all pinned to a RegistrationGeneration and a
// CoordinatorEpoch so that a restarted worker cannot resurrect an old
// incarnation.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/types/versions.hpp"

namespace gpufleet {

struct Registration {
  NodeId node{};
  WorkerId worker{};
  WorkerBootId worker_boot{};
  AgentVersion agent_version;
  ProtocolVersion protocol_version;
  std::string os_platform;
  DriverVersion driver_version;
  RuntimeVersion runtime_version;
  std::vector<std::string> enumerated_devices;   // canonical device identity strings
  std::vector<std::string> supported_capabilities;
  RegistrationGeneration registration_generation{};
  CoordinatorEpoch epoch{};
};

inline std::vector<std::uint8_t> encode_registration(const Registration& r) {
  ByteWriter w;
  w.u64(r.node.value());
  w.u64(r.worker.value());
  w.u64(r.worker_boot.value());
  w.u32(r.agent_version.version.major);
  w.u32(r.agent_version.version.minor);
  w.u32(r.agent_version.version.patch);
  w.u32(r.protocol_version.value);
  w.string(r.os_platform, 128);
  w.string(r.driver_version.text, 128);
  w.u32(r.runtime_version.major);
  w.u32(r.runtime_version.minor);
  w.u32(static_cast<std::uint32_t>(r.enumerated_devices.size()));
  for (auto& d : r.enumerated_devices) w.string(d, 256);
  w.u32(static_cast<std::uint32_t>(r.supported_capabilities.size()));
  for (auto& c : r.supported_capabilities) w.string(c, 128);
  w.u64(r.registration_generation.value());
  w.u64(r.epoch.value());
  return w.take();
}

inline Result<Registration> decode_registration(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  Registration reg;
  std::uint64_t u;
  std::uint32_t u32v;
  if (!r.u64(u)) return error_result(Status::Truncated, "node truncated");
  reg.node = NodeId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "worker truncated");
  reg.worker = WorkerId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "boot truncated");
  reg.worker_boot = WorkerBootId(u);
  if (!r.u32(u32v)) return error_result(Status::Truncated, "agent major truncated");
  reg.agent_version.version.major = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "agent minor truncated");
  reg.agent_version.version.minor = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "agent patch truncated");
  reg.agent_version.version.patch = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "protocol truncated");
  reg.protocol_version.value = u32v;
  auto s = r.string(128); if (!s.ok()) return error_result(Status::Truncated, "os truncated");
  reg.os_platform = s.move_value();
  s = r.string(128); if (!s.ok()) return error_result(Status::Truncated, "driver truncated");
  reg.driver_version.text = s.move_value();
  if (!r.u32(u32v)) return error_result(Status::Truncated, "runtime major truncated");
  reg.runtime_version.major = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "runtime minor truncated");
  reg.runtime_version.minor = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "dev count truncated");
  if (u32v > 4096) return error_result(Status::OutOfBounds, "too many devices");
  for (std::uint32_t k = 0; k < u32v; ++k) {
    auto d = r.string(256); if (!d.ok()) return error_result(Status::Truncated, "device truncated");
    reg.enumerated_devices.push_back(d.move_value());
  }
  if (!r.u32(u32v)) return error_result(Status::Truncated, "cap count truncated");
  if (u32v > 4096) return error_result(Status::OutOfBounds, "too many capabilities");
  for (std::uint32_t k = 0; k < u32v; ++k) {
    auto c = r.string(128); if (!c.ok()) return error_result(Status::Truncated, "capability truncated");
    reg.supported_capabilities.push_back(c.move_value());
  }
  if (!r.u64(u)) return error_result(Status::Truncated, "reg gen truncated");
  reg.registration_generation = RegistrationGeneration(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "epoch truncated");
  reg.epoch = CoordinatorEpoch(u);
  if (!r.done()) return error_result(Status::Malformed, "trailing garbage after registration");
  return ok_result(std::move(reg));
}

}  // namespace gpufleet
