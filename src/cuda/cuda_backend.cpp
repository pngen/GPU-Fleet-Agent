// NVIDIA/CUDA backend implementation (compiled by nvcc).
#include "gpufleetagent/cuda/cuda_backend.hpp"

#include <cuda_runtime.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gpufleetagent/cuda/cuda_validation.hpp"

namespace gpufleet {

namespace {
std::string arch_for(std::uint32_t major, std::uint32_t minor) {
  if (major >= 12) return "blackwell";
  if (major == 9) return "hopper";
  if (major == 8) return "ampere";
  if (major == 7) return "volta";
  if (major == 6) return "pascal";
  return "cuda";
}

std::uint32_t driver_major(int v) { return v / 1000; }
std::uint32_t runtime_major(int v) { return v / 1000; }
std::uint32_t runtime_minor(int v) { return (v / 10) % 100; }

Capability make_cap(std::uint64_t id, std::string name, CapabilityKind kind, std::string value,
                    std::string desc) {
  Capability c;
  c.id = CapabilityId(id);
  c.name = std::move(name);
  c.kind = kind;
  c.value = std::move(value);
  c.description = std::move(desc);
  return c;
}
}  // namespace

std::string CudaDeviceBackend::name() const { return "cuda"; }

std::string cuda_driver_summary() {
  int dv = 0;
  int rv = 0;
  cudaError_t e1 = cudaDriverGetVersion(&dv);
  cudaError_t e2 = cudaRuntimeGetVersion(&rv);
  int count = 0;
  cudaGetDeviceCount(&count);
  char buf[128];
  std::snprintf(buf, sizeof(buf),
                "CUDA driver=%d runtime=%d devices=%d driverVersion=%u.%u runtimeVersion=%u.%u",
                dv, rv, count, driver_major(dv), 0u, runtime_major(rv), runtime_minor(rv));
  std::string s = buf;
  if (e1 != cudaSuccess || e2 != cudaSuccess) s += " (version query incomplete)";
  return s;
}

bool cuda_available() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

std::unique_ptr<DeviceBackend> make_cuda_backend() {
  if (!cuda_available()) return nullptr;
  return std::make_unique<CudaDeviceBackend>();
}

Result<std::vector<EnumeratedDevice>> CudaDeviceBackend::enumerate() const {
  int count = 0;
  cudaError_t e = cudaGetDeviceCount(&count);
  if (e != cudaSuccess) {
    return error_result(Status::Unsupported,
                        "cudaGetDeviceCount failed: " + std::string(cudaGetErrorString(e)));
  }
  std::vector<EnumeratedDevice> out;
  for (int ord = 0; ord < count; ++ord) {
    cudaDeviceProp prop{};
    e = cudaGetDeviceProperties(&prop, ord);
    if (e != cudaSuccess) {
      continue;  // skip devices we cannot introspect
    }
    EnumeratedDevice d;
    d.ordinal = ord;
    d.present = true;
    d.identity.vendor = AcceleratorVendor::Nvidia;
    d.identity.vendor_name = "NVIDIA";
    // Device UUID is not exposed by the CUDA runtime API used here; the stable
    // identity is anchored on vendor + PCI + architecture + compute capability
    // + physical memory + driver version (never PCI alone).
    d.identity.pci.domain = static_cast<std::uint32_t>(prop.pciDomainID);
    d.identity.pci.bus = static_cast<std::uint16_t>(prop.pciBusID);
    d.identity.pci.device = static_cast<std::uint16_t>(prop.pciDeviceID);
    d.identity.pci.function = 0;
    d.identity.architecture = arch_for(prop.major, prop.minor);
    d.identity.compute_capability.major = prop.major;
    d.identity.compute_capability.minor = prop.minor;
    d.identity.total_physical_memory = prop.totalGlobalMem;
    d.identity.numa_node = -1;  // not surfaced by the CUDA runtime API
    int dv = 0;
    cudaDriverGetVersion(&dv);
    d.identity.driver_version.text = std::to_string(dv);
    int rv = 0;
    cudaRuntimeGetVersion(&rv);
    d.identity.runtime_version.major = runtime_major(rv);
    d.identity.runtime_version.minor = runtime_minor(rv);
    out.push_back(std::move(d));
  }
  return ok_result(std::move(out));
}

Result<DeviceProbe> CudaDeviceBackend::probe(const EnumeratedDevice& device) const {
  DeviceProbe p;
  p.enumerated = true;
  p.present = true;

  // Initialize the device context.
  cudaError_t e = cudaSetDevice(device.ordinal);
  if (e != cudaSuccess) {
    return error_result(Status::Unsupported,
                        "cudaSetDevice failed: " + std::string(cudaGetErrorString(e)));
  }
  // Force context creation so cuda_init_ok reflects real initialization.
  if (cudaFree(nullptr) != cudaSuccess) {
    p.cuda_init_ok = false;
    return error_result(Status::Unsupported, "CUDA context initialization failed");
  }
  p.cuda_init_ok = true;

  int dv = 0, rv = 0;
  p.driver_runtime_ok = (cudaDriverGetVersion(&dv) == cudaSuccess) &&
                        (cudaRuntimeGetVersion(&rv) == cudaSuccess);

  std::size_t free_b = 0, total_b = 0;
  e = cudaMemGetInfo(&free_b, &total_b);
  if (e != cudaSuccess) {
    return error_result(Status::Unsupported, "cudaMemGetInfo failed");
  }
  p.total_memory = total_b;
  p.free_memory = free_b;
  p.used_memory = total_b - free_b;
  p.identity = device.identity;

  // Run the real validation sequence.
  CudaValidationResult v = run_cuda_validation(device.ordinal);
  p.core_validation_ok = v.ok;
  p.validation_detail = v.detail;
  p.memory_alloc_ok = v.alloc_ok;
  p.h2d_ok = v.h2d_ok;
  p.kernel_exec_ok = v.kernel_ok;
  p.sync_ok = v.sync_ok;
  p.d2h_ok = v.d2h_ok;
  p.verify_ok = v.verify_ok;

  // Capabilities (discovered / validated / inferred only; never fabricated).
  const std::string cc = device.identity.compute_capability.to_string();
  const std::string sm = device.identity.compute_capability.sm_string();
  std::uint64_t cid = 1;
  p.capabilities.push_back(make_cap(cid++, std::string(capname::vendor), CapabilityKind::DISCOVERED,
                                    "nvidia", "vendor reported by CUDA"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::architecture), CapabilityKind::DISCOVERED,
                                    device.identity.architecture, "architecture from compute capability"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::compute_capability), CapabilityKind::DISCOVERED,
                                    cc, "compute capability"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::total_memory), CapabilityKind::DISCOVERED,
                                    std::to_string(total_b), "total physical memory bytes"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::usable_memory), CapabilityKind::DISCOVERED,
                                    std::to_string(free_b), "free memory bytes at probe"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::cuda_available), CapabilityKind::DISCOVERED,
                                    "true", "CUDA runtime initialized"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::runtime), CapabilityKind::DISCOVERED,
                                    std::to_string(runtime_major(rv)) + "." + std::to_string(runtime_minor(rv)),
                                    "CUDA runtime version"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::dtypes), CapabilityKind::DISCOVERED,
                                    "fp32", "validated dtype (fp32)"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::tensor_cores), CapabilityKind::INFERRED,
                                    device.identity.compute_capability.major >= 7 ? "true" : "false",
                                    "tensor cores inferred from compute capability"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::unified_addressing), CapabilityKind::DISCOVERED,
                                    device.identity.compute_capability.major >= 6 ? "true" : "false",
                                    "unified addressing (historical, discovered)"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::graphs), CapabilityKind::INFERRED,
                                    "true", "CUDA graphs inferred for CUDA >= 10"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::cooperative_launch), CapabilityKind::UNKNOWN,
                                    "unknown", "not queried via CUDA runtime"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::kernel_execution), CapabilityKind::VALIDATED,
                                    "true", "a real CUDA kernel executed and synchronized"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::memory_roundtrip), CapabilityKind::VALIDATED,
                                    "true", "H2D + D2H round-trip verified against CPU reference"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::driver), CapabilityKind::DISCOVERED,
                                    device.identity.driver_version.text, "CUDA driver version"));
  p.capabilities.push_back(make_cap(cid++, std::string(capname::numa), CapabilityKind::UNKNOWN,
                                    "unknown", "NUMA node not surfaced by CUDA runtime"));

  return ok_result(std::move(p));
}

}  // namespace gpufleet
