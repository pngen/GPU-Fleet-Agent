#pragma once
// Deterministic codec for DeviceObservation, used to ship a worker's device
// snapshot inside a DEVICE_SNAPSHOT message payload.
#include <cstdint>
#include <span>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

namespace gpufleet {

inline void write_optional_u32(ByteWriter& w, std::optional<std::uint32_t> v) {
  w.u8(v.has_value() ? 1 : 0);
  if (v.has_value()) w.u32(*v);
}

inline Result<std::optional<std::uint32_t>> read_optional_u32(ByteReader& r) {
  std::uint8_t present = 0;
  if (!r.u8(present)) return error_result(Status::Truncated, "optional presence truncated");
  if (!present) return ok_result(std::optional<std::uint32_t>{});
  std::uint32_t v = 0;
  if (!r.u32(v)) return error_result(Status::Truncated, "optional value truncated");
  return ok_result(std::optional<std::uint32_t>(v));
}

inline void write_optional_f64(ByteWriter& w, std::optional<double> v) {
  w.u8(v.has_value() ? 1 : 0);
  if (v.has_value()) w.f64(*v);
}

inline Result<std::optional<double>> read_optional_f64(ByteReader& r) {
  std::uint8_t present = 0;
  if (!r.u8(present)) return error_result(Status::Truncated, "optional present truncated");
  if (!present) return ok_result(std::optional<double>{});
  double v = 0;
  if (!r.f64(v)) return error_result(Status::Truncated, "optional double truncated");
  return ok_result(std::optional<double>(v));
}

inline std::vector<std::uint8_t> encode_device_observation(const DeviceObservation& o) {
  ByteWriter w;
  w.u64(o.device_id.value());
  w.u64(o.observation_generation.value());
  w.u64(o.health_generation.value());
  w.i64(o.observed_at);
  w.u64(o.source_worker_boot.value());
  w.u64(o.source_worker.value());
  w.u64(o.source_node.value());
  w.u64(o.epoch.value());
  w.u64(o.device_generation.value());
  w.u8(static_cast<std::uint8_t>(o.health));
  w.string(o.health_explanation, 4096);
  w.u64(o.total_memory);
  w.u64(o.used_memory);
  w.u64(o.free_memory);
  write_optional_f64(w, o.temperature_c);
  write_optional_f64(w, o.power_w);
  w.u32(static_cast<std::uint32_t>(o.capabilities.size()));
  for (auto& c : o.capabilities) {
    w.u64(c.id.value());
    w.string(c.name, 128);
    w.u8(static_cast<std::uint8_t>(c.kind));
    w.string(c.value, 256);
    w.string(c.description, 256);
  }
  w.u8(o.core_validation_ok ? 1 : 0);
  w.string(o.validation_detail, 4096);
  w.u8(o.enumerated ? 1 : 0);
  w.u8(o.present ? 1 : 0);
  w.u8(o.driver_runtime_ok ? 1 : 0);
  w.u8(o.cuda_init_ok ? 1 : 0);
  w.u8(o.memory_alloc_ok ? 1 : 0);
  w.u8(o.h2d_ok ? 1 : 0);
  w.u8(o.kernel_exec_ok ? 1 : 0);
  w.u8(o.sync_ok ? 1 : 0);
  w.u8(o.d2h_ok ? 1 : 0);
  w.u8(o.verify_ok ? 1 : 0);

  // DeviceIdentity serialization.
  w.u8(static_cast<std::uint8_t>(o.identity.vendor));
  w.string(o.identity.vendor_name, 128);
  w.string(o.identity.uuid.value, 128);
  w.u32(o.identity.pci.domain);
  w.u16(o.identity.pci.bus);
  w.u16(o.identity.pci.device);
  w.u16(o.identity.pci.function);
  w.string(o.identity.architecture, 64);
  w.u32(o.identity.compute_capability.major);
  w.u32(o.identity.compute_capability.minor);
  w.u64(o.identity.total_physical_memory);
  w.i64(o.identity.numa_node);
  w.string(o.identity.driver_version.text, 128);
  w.u32(o.identity.runtime_version.major);
  w.u32(o.identity.runtime_version.minor);
  w.u32(o.identity.mig.gpu_instance_id);
  w.u32(o.identity.mig.compute_instance_id);
  w.u8(o.identity.parent_device.has_value() ? 1 : 0);
  if (o.identity.parent_device.has_value()) w.u64(o.identity.parent_device->value());

  return w.take();
}

inline Result<DeviceObservation> decode_device_observation(std::span<const std::uint8_t> data);

inline std::vector<std::uint8_t> encode_observation_batch(const std::vector<DeviceObservation>& obs) {
  ByteWriter w;
  w.u32(static_cast<std::uint32_t>(obs.size()));
  for (auto& o : obs) {
    auto blob = encode_device_observation(o);
    w.u32(static_cast<std::uint32_t>(blob.size()));
    w.raw_bytes(blob);
  }
  return w.take();
}

inline Result<std::vector<DeviceObservation>> decode_observation_batch(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  std::uint32_t count;
  if (!r.u32(count)) return error_result(Status::Truncated, "obs count truncated");
  if (count > 4096) return error_result(Status::OutOfBounds, "obs count too large");
  std::vector<DeviceObservation> out;
  out.reserve(count);
  for (std::uint32_t k = 0; k < count; ++k) {
    std::uint32_t len;
    if (!r.u32(len)) return error_result(Status::Truncated, "obs length truncated");
    if (len > 16 * 1024 * 1024) return error_result(Status::OutOfBounds, "obs blob too large");
    auto blob = r.raw(len);
    if (!blob.ok()) return error_result(Status::Truncated, "obs blob truncated");
    auto o = decode_device_observation(blob.value());
    if (!o.ok()) return error_result(Status::Malformed, "obs blob malformed");
    out.push_back(o.move_value());
  }
  if (!r.done()) return error_result(Status::Malformed, "trailing garbage after observation batch");
  return ok_result(std::move(out));
}

inline Result<DeviceObservation> decode_device_observation(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  DeviceObservation o;
  std::uint64_t u; std::uint32_t u32v;
  if (!r.u64(u)) return error_result(Status::Truncated, "device_id truncated");
  o.device_id = DeviceId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "obs_gen truncated");
  o.observation_generation = ObservationGeneration(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "health_gen truncated");
  o.health_generation = HealthGeneration(u);
  std::int64_t i;
  if (!r.i64(i)) return error_result(Status::Truncated, "observed_at truncated");
  o.observed_at = i;
  if (!r.u64(u)) return error_result(Status::Truncated, "worker_boot truncated");
  o.source_worker_boot = WorkerBootId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "source_worker truncated");
  o.source_worker = WorkerId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "source_node truncated");
  o.source_node = NodeId(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "epoch truncated");
  o.epoch = CoordinatorEpoch(u);
  if (!r.u64(u)) return error_result(Status::Truncated, "device_gen truncated");
  o.device_generation = DeviceGeneration(u);
  std::uint8_t hs;
  if (!r.u8(hs)) return error_result(Status::Truncated, "health truncated");
  o.health = static_cast<HealthState>(hs);
  auto expl = r.string(4096); if (!expl.ok()) return error_result(Status::Truncated, "health_explanation truncated");
  o.health_explanation = expl.move_value();
  if (!r.u64(u)) return error_result(Status::Truncated, "total_memory truncated");
  o.total_memory = u;
  if (!r.u64(u)) return error_result(Status::Truncated, "used_memory truncated");
  o.used_memory = u;
  if (!r.u64(u)) return error_result(Status::Truncated, "free_memory truncated");
  o.free_memory = u;
  auto t = read_optional_f64(r); if (!t.ok()) return error_result(Status::Truncated, "temperature truncated");
  o.temperature_c = t.move_value();
  auto pw = read_optional_f64(r); if (!pw.ok()) return error_result(Status::Truncated, "power truncated");
  o.power_w = pw.move_value();
  std::uint32_t ncap;
  if (!r.u32(ncap)) return error_result(Status::Truncated, "cap count truncated");
  if (ncap > 4096) return error_result(Status::OutOfBounds, "cap count exceeds bound");
  for (std::uint32_t k = 0; k < ncap; ++k) {
    Capability c;
    if (!r.u64(u)) return error_result(Status::Truncated, "cap id truncated");
    c.id = CapabilityId(u);
    auto nm = r.string(128); if (!nm.ok()) return error_result(Status::Truncated, "cap name truncated");
    c.name = nm.move_value();
    std::uint8_t kind; if (!r.u8(kind)) return error_result(Status::Truncated, "cap kind truncated");
    c.kind = static_cast<CapabilityKind>(kind);
    auto val = r.string(256); if (!val.ok()) return error_result(Status::Truncated, "cap value truncated");
    c.value = val.move_value();
    auto desc = r.string(256); if (!desc.ok()) return error_result(Status::Truncated, "cap desc truncated");
    c.description = desc.move_value();
    o.capabilities.push_back(std::move(c));
  }
  std::uint8_t b;
  if (!r.u8(b)) return error_result(Status::Truncated, "core_valid truncated");
  o.core_validation_ok = b != 0;
  auto vd = r.string(4096); if (!vd.ok()) return error_result(Status::Truncated, "validation_detail truncated");
  o.validation_detail = vd.move_value();
  if (!r.u8(b)) return error_result(Status::Truncated, "enumerated truncated");
  o.enumerated = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "present truncated");
  o.present = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "driver_runtime truncated");
  o.driver_runtime_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "cuda_init truncated");
  o.cuda_init_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "malloc truncated");
  o.memory_alloc_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "h2d truncated");
  o.h2d_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "kernel truncated");
  o.kernel_exec_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "sync truncated");
  o.sync_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "d2h truncated");
  o.d2h_ok = b != 0;
  if (!r.u8(b)) return error_result(Status::Truncated, "verify truncated");
  o.verify_ok = b != 0;

  // identity
  std::uint8_t vd8;
  if (!r.u8(vd8)) return error_result(Status::Truncated, "vendor truncated");
  o.identity.vendor = static_cast<AcceleratorVendor>(vd8);
  auto vn = r.string(128); if (!vn.ok()) return error_result(Status::Truncated, "vendor_name truncated");
  o.identity.vendor_name = vn.move_value();
  auto uuid = r.string(128); if (!uuid.ok()) return error_result(Status::Truncated, "uuid truncated");
  o.identity.uuid.value = uuid.move_value();
  if (!r.u32(o.identity.pci.domain)) return error_result(Status::Truncated, "pci domain truncated");
  if (!r.u16(o.identity.pci.bus)) return error_result(Status::Truncated, "pci bus truncated");
  if (!r.u16(o.identity.pci.device)) return error_result(Status::Truncated, "pci dev truncated");
  if (!r.u16(o.identity.pci.function)) return error_result(Status::Truncated, "pci fn truncated");
  auto arch = r.string(64); if (!arch.ok()) return error_result(Status::Truncated, "arch truncated");
  o.identity.architecture = arch.move_value();
  if (!r.u32(o.identity.compute_capability.major)) return error_result(Status::Truncated, "cc major truncated");
  if (!r.u32(o.identity.compute_capability.minor)) return error_result(Status::Truncated, "cc minor truncated");
  if (!r.u64(u)) return error_result(Status::Truncated, "total mem truncated");
  o.identity.total_physical_memory = u;
  if (!r.i64(i)) return error_result(Status::Truncated, "numa truncated");
  o.identity.numa_node = static_cast<std::int32_t>(i);
  auto dv = r.string(128); if (!dv.ok()) return error_result(Status::Truncated, "driver truncated");
  o.identity.driver_version.text = dv.move_value();
  if (!r.u32(u32v)) return error_result(Status::Truncated, "runtime major truncated");
  o.identity.runtime_version.major = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "runtime minor truncated");
  o.identity.runtime_version.minor = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "mig gpu truncated");
  o.identity.mig.gpu_instance_id = u32v;
  if (!r.u32(u32v)) return error_result(Status::Truncated, "mig ci truncated");
  o.identity.mig.compute_instance_id = u32v;
  std::uint8_t hasp; if (!r.u8(hasp)) return error_result(Status::Truncated, "parent presence truncated");
  if (hasp) { if (!r.u64(u)) return error_result(Status::Truncated, "parent truncated"); o.identity.parent_device = DeviceId(u); }

  if (!r.done()) return error_result(Status::Malformed, "trailing garbage in observation blob");
  return ok_result(std::move(o));
}

}  // namespace gpufleet
