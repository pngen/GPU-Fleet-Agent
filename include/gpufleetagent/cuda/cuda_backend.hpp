#pragma once
// NVIDIA/CUDA probing backend.
//
// This backend gathers only information actually available through supported
// APIs (CUDA Runtime, CUDA Driver). NVML is optional and not required: the
// backend functions with CUDA alone. It performs the real validation sequence
// for each present device and records the resulting capabilities.
#include <memory>
#include <string>

#include "gpufleetagent/device/device_backend.hpp"

namespace gpufleet {

/// Factory: returns a CUDA backend if CUDA is available and enumerable,
/// otherwise nullptr. NVML absence is not a failure.
std::unique_ptr<DeviceBackend> make_cuda_backend();

/// True when the CUDA runtime reports at least one device.
bool cuda_available();

/// A human-readable summary of the CUDA toolkit / driver on the host.
std::string cuda_driver_summary();

/// The concrete CUDA backend (declared here; implementation is nvcc-compiled).
class CudaDeviceBackend final : public DeviceBackend {
 public:
  ~CudaDeviceBackend() override = default;
  std::string name() const override;
  bool available() const override { return cuda_available(); }
  Result<std::vector<EnumeratedDevice>> enumerate() const override;
  Result<DeviceProbe> probe(const EnumeratedDevice& device) const override;
};

}  // namespace gpufleet
