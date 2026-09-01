#pragma once
// Device probing backend abstraction.
//
// GPU Fleet Agent does not depend on a single vendor API. A DeviceBackend
// enumerates local accelerator devices and probes them, gathering only what
// the underlying platform APIs actually expose. The NVIDIA/CUDA backend is the
// concrete implementation; other accelerator providers can be added by
// implementing this interface. NVML is optional: the CUDA backend continues to
// function (enumerate, initialize, allocate, execute, synchronize, round-trip)
// using the CUDA Runtime/Driver APIs alone, and only adds NVML data when NVML
// is present.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/identity/device_identity.hpp"
#include "gpufleetagent/capability/capability.hpp"

namespace gpufleet {

/// An enumerated, statically-identified accelerator.
struct EnumeratedDevice {
  int ordinal = -1;               // backend index, e.g. CUDA device ordinal
  DeviceIdentity identity;        // stable identity resolved by the backend
  bool present = false;           // reportable/present right now
};

/// The dynamic, probed state of one device, including validation evidence.
struct DeviceProbe {
  DeviceIdentity identity;
  bool enumerated = false;
  bool present = false;
  bool driver_runtime_ok = false;
  bool cuda_init_ok = false;
  std::uint64_t total_memory = 0;
  std::uint64_t used_memory = 0;
  std::uint64_t free_memory = 0;
  std::optional<double> temperature_c;   // only when the platform reports it
  std::optional<double> power_w;         // only when the platform reports it

  // Per-step validation evidence. A backend that performs the full validation
  // sequence records each sub-step independently so that health evaluation can
  // be precise about WHAT failed, not merely that something did.
  bool memory_alloc_ok = false;
  bool h2d_ok = false;
  bool kernel_exec_ok = false;
  bool sync_ok = false;
  bool d2h_ok = false;
  bool verify_ok = false;
  bool core_validation_ok = false;       // alloc+copy+kernel+sync+copy+verify passed
  std::string validation_detail;
  std::vector<Capability> capabilities;  // discovered capabilities
};

/// Abstract probe backend. Implementations must be safe to call from the
/// probing thread and must not block on locks held elsewhere.
class DeviceBackend {
 public:
  virtual ~DeviceBackend() = default;
  virtual std::string name() const = 0;
  /// Whether the backend can operate at all (e.g. CUDA toolkit present).
  virtual bool available() const = 0;
  virtual Result<std::vector<EnumeratedDevice>> enumerate() const = 0;
  virtual Result<DeviceProbe> probe(const EnumeratedDevice& device) const = 0;
};

}  // namespace gpufleet
