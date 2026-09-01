#include "gpufleetagent/device/synthetic_backend.hpp"
#include "gpufleetagent/capability/capability.hpp"

namespace gpufleet {

namespace {
std::string identity_for(std::size_t idx, std::uint64_t mem) {
  return "synthetic:" + std::to_string(idx) + ":" + std::to_string(mem);
}
}  // namespace

SyntheticDeviceBackend::SyntheticDeviceBackend(std::size_t count, std::uint64_t memory_bytes)
    : count_(count == 0 ? 1 : count), memory_bytes_(memory_bytes) {}

Result<std::vector<EnumeratedDevice>> SyntheticDeviceBackend::enumerate() const {
  std::vector<EnumeratedDevice> out;
  for (std::size_t i = 0; i < count_; ++i) {
    EnumeratedDevice d;
    d.ordinal = static_cast<int>(i);
    d.present = true;
    d.identity.vendor = AcceleratorVendor::Nvidia;  // synthetic label below
    d.identity.vendor_name = "synthetic";
    d.identity.uuid.value = identity_for(i, memory_bytes_);
    d.identity.architecture = "synthetic";
    d.identity.compute_capability.major = 12;
    d.identity.compute_capability.minor = 0;
    d.identity.total_physical_memory = memory_bytes_;
    d.identity.driver_version.text = "synthetic";
    out.push_back(std::move(d));
  }
  return ok_result(std::move(out));
}

Result<DeviceProbe> SyntheticDeviceBackend::probe(const EnumeratedDevice& device) const {
  DeviceProbe p;
  p.enumerated = true;
  p.present = true;
  p.driver_runtime_ok = true;
  p.cuda_init_ok = true;
  p.identity = device.identity;
  p.total_memory = memory_bytes_;
  p.used_memory = 0;
  p.free_memory = memory_bytes_;
  p.memory_alloc_ok = true;
  p.h2d_ok = true;
  p.kernel_exec_ok = true;
  p.sync_ok = true;
  p.d2h_ok = true;
  p.verify_ok = true;
  p.core_validation_ok = true;
  p.validation_detail = "synthetic validation OK (no physical accelerator involved)";
  std::uint64_t cid = 1;
  Capability c;
  c.id = CapabilityId(cid);
  c.name = std::string(capname::vendor);
  c.kind = CapabilityKind::INFERRED;
  c.value = "synthetic";
  c.description = "synthetic vendor (not physical hardware)";
  p.capabilities.push_back(std::move(c));
  Capability ck;
  ck.id = CapabilityId(cid + 1);
  ck.name = std::string(capname::kernel_execution);
  ck.kind = CapabilityKind::VALIDATED;
  ck.value = "true";
  ck.description = "synthetic kernel execution (no physical accelerator involved)";
  p.capabilities.push_back(std::move(ck));
  return ok_result(std::move(p));
}

}  // namespace gpufleet
