#pragma once
// Deterministic snapshot codec. Serializes the full StateSnapshot so that it
// can be persisted and/or shipped in a SNAPSHOT_RESPONSE. Encoding is
// big-endian, order-stable (maps iterate in key order), and bounded.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/observation/observation.hpp"

namespace gpufleet {

inline void write_identity(ByteWriter& w, const DeviceIdentity& id) {
  w.u8(static_cast<std::uint8_t>(id.vendor));
  w.string(id.vendor_name, 128);
  w.string(id.uuid.value, 128);
  w.u32(id.pci.domain); w.u16(id.pci.bus); w.u16(id.pci.device); w.u16(id.pci.function);
  w.string(id.architecture, 64);
  w.u32(id.compute_capability.major); w.u32(id.compute_capability.minor);
  w.u64(id.total_physical_memory);
  w.i64(id.numa_node);
  w.string(id.driver_version.text, 128);
  w.u32(id.runtime_version.major); w.u32(id.runtime_version.minor);
  w.u32(id.mig.gpu_instance_id); w.u32(id.mig.compute_instance_id);
  w.u8(id.parent_device.has_value() ? 1 : 0);
  if (id.parent_device.has_value()) w.u64(id.parent_device->value());
}

inline bool read_identity(ByteReader& r, DeviceIdentity& id) {
  std::uint8_t v;
  std::uint32_t u32v;
  std::uint64_t u64v;
  std::int64_t i64v;
  std::uint16_t u16v;
  if (!r.u8(v)) return false; id.vendor = static_cast<AcceleratorVendor>(v);
  auto s = r.string(128); if (!s.ok()) return false; id.vendor_name = s.move_value();
  s = r.string(128); if (!s.ok()) return false; id.uuid.value = s.move_value();
  if (!r.u32(u32v)) return false; id.pci.domain = u32v;
  if (!r.u16(u16v)) return false; id.pci.bus = u16v;
  if (!r.u16(u16v)) return false; id.pci.device = u16v;
  if (!r.u16(u16v)) return false; id.pci.function = u16v;
  s = r.string(64); if (!s.ok()) return false; id.architecture = s.move_value();
  if (!r.u32(u32v)) return false; id.compute_capability.major = u32v;
  if (!r.u32(u32v)) return false; id.compute_capability.minor = u32v;
  if (!r.u64(u64v)) return false; id.total_physical_memory = u64v;
  if (!r.i64(i64v)) return false; id.numa_node = static_cast<std::int32_t>(i64v);
  s = r.string(128); if (!s.ok()) return false; id.driver_version.text = s.move_value();
  if (!r.u32(u32v)) return false; id.runtime_version.major = u32v;
  if (!r.u32(u32v)) return false; id.runtime_version.minor = u32v;
  if (!r.u32(u32v)) return false; id.mig.gpu_instance_id = u32v;
  if (!r.u32(u32v)) return false; id.mig.compute_instance_id = u32v;
  std::uint8_t hasp; if (!r.u8(hasp)) return false;
  if (hasp) { if (!r.u64(u64v)) return false; id.parent_device = DeviceId(u64v); }
  return true;
}

inline void write_device_state(ByteWriter& w, const DeviceState& d) {
  w.u64(d.device_id.value());
  w.u64(d.generation.value());
  write_identity(w, d.identity);
  w.u8(d.present ? 1 : 0);
  w.u8(static_cast<std::uint8_t>(d.health));
  w.string(d.health_explanation, 4096);
  w.u8(static_cast<std::uint8_t>(d.eligibility));
  w.u32(static_cast<std::uint32_t>(d.eligibility_reasons.size()));
  for (auto r : d.eligibility_reasons) w.u8(static_cast<std::uint8_t>(r));
  w.u8(static_cast<std::uint8_t>(d.drain));
  w.u8(d.quarantined ? 1 : 0);
  w.string(d.quarantine.reason, 512);
  w.string(d.quarantine.source, 128);
  w.u64(d.quarantine.generation.value());
  w.i64(d.quarantine.at);
  w.u64(d.quarantine.device_id.value());
  w.u64(d.quarantine.authority.value());
  w.u64(d.total_memory); w.u64(d.used_memory); w.u64(d.free_memory);
  w.u8(d.temperature_c.has_value() ? 1 : 0); if (d.temperature_c.has_value()) w.f64(*d.temperature_c);
  w.u8(d.power_w.has_value() ? 1 : 0); if (d.power_w.has_value()) w.f64(*d.power_w);
  w.u32(static_cast<std::uint32_t>(d.capabilities.size()));
  for (auto& c : d.capabilities) {
    w.u64(c.id.value()); w.string(c.name, 128); w.u8(static_cast<std::uint8_t>(c.kind));
    w.string(c.value, 256); w.string(c.description, 256);
  }
  w.u8(d.core_validation_ok ? 1 : 0);
  w.string(d.validation_detail, 4096);
  w.i64(d.last_observed_at); w.i64(d.last_validated_at); w.i64(d.last_authoritative_at);
  w.u64(d.observation_generation.value());
  w.u64(d.health_generation.value());
  w.u64(d.source_worker_boot.value());
  w.u8(d.observation_fresh ? 1 : 0);
}

inline bool read_device_state(ByteReader& r, DeviceState& d) {
  std::uint64_t u; std::uint32_t u32v; std::int64_t i64v; std::uint8_t b;
  if (!r.u64(u)) return false; d.device_id = DeviceId(u);
  if (!r.u64(u)) return false; d.generation = DeviceGeneration(u);
  if (!read_identity(r, d.identity)) return false;
  if (!r.u8(b)) return false; d.present = b != 0;
  if (!r.u8(b)) return false; d.health = static_cast<HealthState>(b);
  auto s = r.string(4096); if (!s.ok()) return false; d.health_explanation = s.move_value();
  if (!r.u8(b)) return false; d.eligibility = static_cast<EligibilityState>(b);
  if (!r.u32(u32v)) return false;
  if (u32v > 32) return false;
  for (std::uint32_t k = 0; k < u32v; ++k) {
    std::uint8_t rb; if (!r.u8(rb)) return false;
    d.eligibility_reasons.push_back(static_cast<EligibilityReason>(rb));
  }
  if (!r.u8(b)) return false; d.drain = static_cast<DrainState>(b);
  if (!r.u8(b)) return false; d.quarantined = b != 0;
  s = r.string(512); if (!s.ok()) return false; d.quarantine.reason = s.move_value();
  s = r.string(128); if (!s.ok()) return false; d.quarantine.source = s.move_value();
  if (!r.u64(u)) return false; d.quarantine.generation = DeviceGeneration(u);
  if (!r.i64(i64v)) return false; d.quarantine.at = i64v;
  if (!r.u64(u)) return false; d.quarantine.device_id = DeviceId(u);
  if (!r.u64(u)) return false; d.quarantine.authority = CoordinatorEpoch(u);
  if (!r.u64(u)) return false; d.total_memory = u;
  if (!r.u64(u)) return false; d.used_memory = u;
  if (!r.u64(u)) return false; d.free_memory = u;
  if (!r.u8(b)) return false; if (b) { double dv; if (!r.f64(dv)) return false; d.temperature_c = dv; }
  if (!r.u8(b)) return false; if (b) { double dv; if (!r.f64(dv)) return false; d.power_w = dv; }
  if (!r.u32(u32v)) return false;
  if (u32v > 4096) return false;
  for (std::uint32_t k = 0; k < u32v; ++k) {
    Capability c;
    if (!r.u64(u)) return false; c.id = CapabilityId(u);
    s = r.string(128); if (!s.ok()) return false; c.name = s.move_value();
    if (!r.u8(b)) return false; c.kind = static_cast<CapabilityKind>(b);
    s = r.string(256); if (!s.ok()) return false; c.value = s.move_value();
    s = r.string(256); if (!s.ok()) return false; c.description = s.move_value();
    d.capabilities.push_back(std::move(c));
  }
  if (!r.u8(b)) return false; d.core_validation_ok = b != 0;
  s = r.string(4096); if (!s.ok()) return false; d.validation_detail = s.move_value();
  if (!r.i64(i64v)) return false; d.last_observed_at = i64v;
  if (!r.i64(i64v)) return false; d.last_validated_at = i64v;
  if (!r.i64(i64v)) return false; d.last_authoritative_at = i64v;
  if (!r.u64(u)) return false; d.observation_generation = ObservationGeneration(u);
  if (!r.u64(u)) return false; d.health_generation = HealthGeneration(u);
  if (!r.u64(u)) return false; d.source_worker_boot = WorkerBootId(u);
  if (!r.u8(b)) return false; d.observation_fresh = b != 0;
  return true;
}

inline std::vector<std::uint8_t> encode_snapshot(const StateSnapshot& s) {
  ByteWriter w;
  w.u64(s.coordinator.value());
  w.u64(s.epoch.value());
  w.u64(s.fleet_generation.value());
  w.u64(s.node_generation.value());
  w.u32(static_cast<std::uint32_t>(s.nodes.size()));
  for (auto& [k, n] : s.nodes) { w.u64(k.value()); w.u64(n.generation.value()); w.i64(n.first_seen); }
  w.u32(static_cast<std::uint32_t>(s.workers.size()));
  for (auto& [k, wr] : s.workers) {
    w.u64(k.value());
    w.u64(wr.boot.value());
    w.u64(wr.registration_generation.value());
    w.u64(wr.node.value());
    w.u64(wr.agent.value());
    w.u32(wr.agent_version.version.major);
    w.u32(wr.agent_version.version.minor);
    w.u32(wr.agent_version.version.patch);
    w.u32(wr.protocol_version.value);
    w.string(wr.os_platform, 128);
    w.string(wr.driver_version.text, 128);
    w.u32(static_cast<std::uint32_t>(wr.device_identities.size()));
    for (auto& x : wr.device_identities) w.string(x, 256);
    w.u32(static_cast<std::uint32_t>(wr.supported_capabilities.size()));
    for (auto& x : wr.supported_capabilities) w.string(x, 128);
    w.i64(wr.registered_at); w.i64(wr.last_heartbeat);
    w.u64(wr.registered_epoch.value());
    w.u8(wr.alive ? 1 : 0); w.u8(wr.fenced ? 1 : 0); w.u8(wr.lost ? 1 : 0);
  }
  w.u32(static_cast<std::uint32_t>(s.devices.size()));
  for (auto& [key, d] : s.devices) { w.string(key, 256); write_device_state(w, d); }
  w.u32(static_cast<std::uint32_t>(s.change_log.size()));
  for (auto& c : s.change_log) {
    w.u8(static_cast<std::uint8_t>(c.kind));
    w.u64(c.device_id.value());
    w.i64(c.at);
    w.u64(c.generation.value());
    w.u64(c.epoch.value());
    w.string(c.detail, 512);
    w.string(c.from, 128);
    w.string(c.to, 128);
  }
  return w.take();
}

inline Result<StateSnapshot> decode_snapshot(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  StateSnapshot s;
  std::uint64_t u; std::uint32_t u32v; std::int64_t i64v; std::uint8_t b;
  if (!r.u64(u)) return error_result(Status::Truncated, "coordinator truncated");
  s.coordinator = CoordinatorId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "epoch truncated");
  s.epoch = CoordinatorEpoch(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "fleet gen truncated");
  s.fleet_generation = FleetGeneration(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "node gen truncated");
  s.node_generation = NodeGeneration(u);
  if (!r.u32(u32v)) return error_result(Status::Truncated, "node count truncated");
  if (u32v > 100000) return error_result(Status::OutOfBounds, "node count too large");
  for (std::uint32_t k = 0; k < u32v; ++k) {
    NodeRecord n;
    if (!r.u64(u)) return error_result(Status::Truncated, "node id truncated");
    n.node = NodeId(u);
    if (!r.u64(u)) return error_result(Status::Truncated, "node gen truncated");
    n.generation = NodeGeneration(u);
    if (!r.i64(i64v)) return error_result(Status::Truncated, "node first_seen truncated");
    n.first_seen = i64v;
    s.nodes.emplace(n.node, n);
  }
  std::uint32_t worker_count = 0;
  if (!r.u32(worker_count)) return error_result(Status::Truncated, "worker count truncated");
  if (worker_count > 100000) return error_result(Status::OutOfBounds, "worker count too large");
  for (std::uint32_t k = 0; k < worker_count; ++k) {
    WorkerRecord wr;
    if (!r.u64(u)) return error_result(Status::Truncated, "worker id truncated");
    wr.worker = WorkerId(u);
    if (!r.u64(u)) return error_result(Status::Truncated, "worker boot truncated");
    wr.boot = WorkerBootId(u);
    if (!r.u64(u)) return error_result(Status::Truncated, "reg gen truncated");
    wr.registration_generation = RegistrationGeneration(u);
    if (!r.u64(u)) return error_result(Status::Truncated, "worker node truncated");
    wr.node = NodeId(u);
    if (!r.u64(u)) return error_result(Status::Truncated, "worker agent truncated");
    wr.agent = AgentId(u);
    if (!r.u32(u32v)) return error_result(Status::Truncated, "agent major truncated");
    wr.agent_version.version.major = u32v;
    if (!r.u32(u32v)) return error_result(Status::Truncated, "agent minor truncated");
    wr.agent_version.version.minor = u32v;
    if (!r.u32(u32v)) return error_result(Status::Truncated, "agent patch truncated");
    wr.agent_version.version.patch = u32v;
    if (!r.u32(u32v)) return error_result(Status::Truncated, "protocol truncated");
    wr.protocol_version.value = u32v;
    auto s2 = r.string(128); if (!s2.ok()) return error_result(Status::Truncated, "os truncated");
    wr.os_platform = s2.move_value();
    s2 = r.string(128); if (!s2.ok()) return error_result(Status::Truncated, "driver truncated");
    wr.driver_version.text = s2.move_value();
    if (!r.u32(u32v)) return error_result(Status::Truncated, "dev ident count truncated");
    if (u32v > 4096) return error_result(Status::OutOfBounds, "dev ident too many");
    for (std::uint32_t j = 0; j < u32v; ++j) { auto x = r.string(256); if (!x.ok()) return error_result(Status::Truncated, "dev ident truncated"); wr.device_identities.push_back(x.move_value()); }
    if (!r.u32(u32v)) return error_result(Status::Truncated, "caps count truncated");
    if (u32v > 4096) return error_result(Status::OutOfBounds, "caps too many");
    for (std::uint32_t j = 0; j < u32v; ++j) { auto x = r.string(128); if (!x.ok()) return error_result(Status::Truncated, "cap truncated"); wr.supported_capabilities.push_back(x.move_value()); }
    if (!r.i64(i64v)) return error_result(Status::Truncated, "registered_at truncated");
    wr.registered_at = i64v;
    if (!r.i64(i64v)) return error_result(Status::Truncated, "last heartbeat truncated");
    wr.last_heartbeat = i64v;
    if (!r.u64(u)) return error_result(Status::Truncated, "reg epoch truncated");
    wr.registered_epoch = CoordinatorEpoch(u);
    if (!r.u8(b)) return error_result(Status::Truncated, "alive truncated");
    wr.alive = b != 0;
    if (!r.u8(b)) return error_result(Status::Truncated, "fenced truncated");
    wr.fenced = b != 0;
    if (!r.u8(b)) return error_result(Status::Truncated, "lost truncated");
    wr.lost = b != 0;
    // avoid duplicate worker ids.
    if (s.workers.count(wr.worker)) return error_result(Status::Duplicate, "duplicate worker id");
    s.workers.emplace(wr.worker, std::move(wr));
  }
  if (!r.u32(u32v)) return error_result(Status::Truncated, "device count truncated");
  if (u32v > 100000) return error_result(Status::OutOfBounds, "device count too large");
  for (std::uint32_t k = 0; k < u32v; ++k) {
    auto key = r.string(256); if (!key.ok()) return error_result(Status::Truncated, "device key truncated");
    DeviceState d;
    if (!read_device_state(r, d)) return error_result(Status::Truncated, "device state truncated");
    if (s.devices.count(key.value())) return error_result(Status::Duplicate, "duplicate device key");
    s.devices.emplace(key.move_value(), std::move(d));
  }
  if (!r.u32(u32v)) return error_result(Status::Truncated, "change count truncated");
  if (u32v > 200000) return error_result(Status::OutOfBounds, "change log too large");
  for (std::uint32_t k = 0; k < u32v; ++k) {
    ChangeRecord c;
    if (!r.u8(b)) return error_result(Status::Truncated, "change kind truncated");
    c.kind = static_cast<ChangeKind>(b);
    if (!r.u64(u)) return error_result(Status::Truncated, "change device truncated");
    c.device_id = DeviceId(u);
    if (!r.i64(i64v)) return error_result(Status::Truncated, "change at truncated");
    c.at = i64v;
    if (!r.u64(u)) return error_result(Status::Truncated, "change gen truncated");
    c.generation = DeviceGeneration(u);
    if (!r.u64(u)) return error_result(Status::Truncated, "change epoch truncated");
    c.epoch = CoordinatorEpoch(u);
    auto sr = r.string(512); if (!sr.ok()) return error_result(Status::Truncated, "change detail truncated");
    c.detail = sr.move_value();
    sr = r.string(128); if (!sr.ok()) return error_result(Status::Truncated, "change from truncated");
    c.from = sr.move_value();
    sr = r.string(128); if (!sr.ok()) return error_result(Status::Truncated, "change to truncated");
    c.to = sr.move_value();
    s.change_log.push_back(std::move(c));
  }
  if (!r.done()) return error_result(Status::Malformed, "trailing garbage after snapshot");
  s.accounting = Accounting{};
  return ok_result(std::move(s));
}

}  // namespace gpufleet
