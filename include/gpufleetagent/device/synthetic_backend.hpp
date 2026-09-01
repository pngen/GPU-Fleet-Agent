#pragma once
// Synthetic device backend.
//
// Used for deterministic, clearly-labeled synthetic fleet and device scenarios
// (tests, examples, and the multiprocess proof) where a fixed set of devices is
// required independent of any physical accelerator. Devices created by this
// backend are always reported with the "synthetic" vendor marker and never
// presented as physical hardware.
#include <cstdint>
#include <string>
#include <vector>

#include "gpufleetagent/device/device_backend.hpp"

namespace gpufleet {

/// A synthetic backend exposing p count fixed devices (default 1). Each
/// device has a deterministic identity, stable memory, and passes a simulated
/// validation sequence. Used in synthetic scenarios only.
class SyntheticDeviceBackend final : public DeviceBackend {
 public:
  explicit SyntheticDeviceBackend(std::size_t count = 1, std::uint64_t memory_bytes = 24ull * 1024 * 1024 * 1024);
  std::string name() const override { return "synthetic"; }
  bool available() const override { return true; }
  Result<std::vector<EnumeratedDevice>> enumerate() const override;
  Result<DeviceProbe> probe(const EnumeratedDevice& device) const override;

 private:
  std::size_t count_;
  std::uint64_t memory_bytes_;
};

}  // namespace gpufleet
