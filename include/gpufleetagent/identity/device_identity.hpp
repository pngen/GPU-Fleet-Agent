#pragma once
// Device identity model.
//
// Stable device identity is deliberately separate from ephemeral device
// identity. A device is identified by a durable identity (vendor, UUID,
// PCI address, architecture, compute capability, physical memory, NUMA
// locality) that survives process restarts, and by a runtime DeviceId that is
// only as stable as the current process incarnation and the device
// enumeration order. PCI position alone is NEVER treated as globally stable
// identity: it can change across reboots and is meaningless across a
// heterogeneous fleet.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/types/versions.hpp"

namespace gpufleet {

/// A PCI address: domain:bus:device.function.
struct PciAddress {
  std::uint32_t domain = 0;
  std::uint16_t bus = 0;
  std::uint16_t device = 0;
  std::uint16_t function = 0;

  bool operator==(const PciAddress&) const = default;
  bool operator!=(const PciAddress&) const = default;

  std::string to_string() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04x:%02x:%02x.%01x", domain, bus, device, function);
    return std::string(buf);
  }
};

/// A device UUID. For NVIDIA devices this is the NVML/nvidia-smi UUID.
struct DeviceUuid {
  std::string value;
  bool empty() const { return value.empty(); }
  bool operator==(const DeviceUuid& o) const { return value == o.value; }
  bool operator!=(const DeviceUuid& o) const { return value != o.value; }
  std::string to_string() const { return value; }
};

/// CUDA compute capability, e.g. (12, 0) for an sm_120 device.
struct ComputeCapability {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  bool operator==(const ComputeCapability& o) const { return major == o.major && minor == o.minor; }
  bool operator!=(const ComputeCapability& o) const { return !(*this == o); }
  std::string to_string() const { return std::to_string(major) + "." + std::to_string(minor); }
  /// e.g. "sm_120"
  std::string sm_string() const {
    if (major == 0) return "unknown";
    return "sm_" + std::to_string(major) + std::to_string(minor);
  }
  // Capability ordering: a compute capability (a,b) is >= (c,d) iff major
  // exceeds, or majors are equal and minor is not lower.
  bool operator<(const ComputeCapability& o) const {
    if (major != o.major) return major < o.major;
    return minor < o.minor;
  }
  bool operator<=(const ComputeCapability& o) const { return *this < o || *this == o; }
  bool operator>(const ComputeCapability& o) const { return o < *this; }
  bool operator>=(const ComputeCapability& o) const { return !(*this < o); }
};

/// Known accelerator vendors. Unknown vendors are retained as opaque text.
enum class AcceleratorVendor : std::uint8_t {
  Unknown = 0,
  Nvidia = 1,
  Amd = 2,
  Intel = 3,
};

constexpr std::string_view to_string(AcceleratorVendor v) {
  switch (v) {
    case AcceleratorVendor::Nvidia: return "nvidia";
    case AcceleratorVendor::Amd: return "amd";
    case AcceleratorVendor::Intel: return "intel";
    case AcceleratorVendor::Unknown:
    default: return "unknown";
  }
}

/// A MIG (multi-instance GPU) compute instance / partition identity, present
/// only where the platform exposes MIG.
struct MigPartition {
  std::uint32_t gpu_instance_id = 0;
  std::uint32_t compute_instance_id = 0;
  bool empty() const { return gpu_instance_id == 0; }
  bool operator==(const MigPartition& o) const {
    return gpu_instance_id == o.gpu_instance_id && compute_instance_id == o.compute_instance_id;
  }
};

/// Durable, cross-restart device identity. This is the identity used to detect
/// whether a device is the same physical device across observations and
/// process incarnations. PCI position alone is not sufficient; the stable
/// identity includes the vendor, UUID (where available), architecture,
/// compute capability, physical memory size, and NUMA/PCI locality.
struct DeviceIdentity {
  AcceleratorVendor vendor = AcceleratorVendor::Unknown;
  std::string vendor_name;
  DeviceUuid uuid;
  PciAddress pci;
  std::string architecture;        // e.g. "blackwell", "hopper", "unknown"
  ComputeCapability compute_capability;
  std::uint64_t total_physical_memory = 0;  // bytes
  std::int32_t numa_node = -1;              // -1 = unknown
  DriverVersion driver_version;
  RuntimeVersion runtime_version;
  MigPartition mig;
  std::optional<DeviceId> parent_device;    // parent for MIG sub-devices

  bool operator==(const DeviceIdentity& o) const {
    return vendor == o.vendor && vendor_name == o.vendor_name && uuid == o.uuid &&
           pci == o.pci && architecture == o.architecture &&
           compute_capability == o.compute_capability &&
           total_physical_memory == o.total_physical_memory && numa_node == o.numa_node &&
           driver_version.text == o.driver_version.text &&
           runtime_version.major == o.runtime_version.major &&
           runtime_version.minor == o.runtime_version.minor && mig == o.mig &&
           parent_device == o.parent_device;
  }
  bool operator!=(const DeviceIdentity& o) const { return !(*this == o); }

  /// True when the identity carries enough durable information to be treated
  /// as a stable identity (at least vendor, and either a UUID or a full PCI
  /// address plus architecture). A raw PCI position alone is never enough.
  bool is_stable() const {
    bool has_pci = pci.domain != 0 || pci.bus != 0 || pci.device != 0 || pci.function != 0;
    bool has_uuid = !uuid.empty();
    bool has_arch = !architecture.empty() && compute_capability != ComputeCapability{};
    return (has_uuid || (has_pci && has_arch));
  }
};

/// A stable identity key for comparison across observations.
struct DeviceIdentityKey {
  std::string value;  // canonical string form used for hashing / equality.
  bool operator==(const DeviceIdentityKey&) const = default;
};

/// Produces a canonical, deterministic string form of a stable identity.
inline std::string canonical_device_identity(const DeviceIdentity& d) {
  std::string s;
  s += to_string(d.vendor);
  s += '|';
  s += d.vendor_name;
  s += '|';
  s += d.uuid.value;
  s += '|';
  s += d.pci.to_string();
  s += '|';
  s += d.architecture;
  s += '|';
  s += d.compute_capability.to_string();
  s += '|';
  s += std::to_string(d.total_physical_memory);
  s += '|';
  s += d.driver_version.text;
  return s;
}

}  // namespace gpufleet
