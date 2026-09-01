#pragma once
// Capability advertisement model.
//
// Capabilities are represented explicitly and are never fabricated. Every
// capability carries a CapabilityKind so callers can distinguish what was
// actually discovered by probing, what was validated by execution, what was
// inferred from hardware model identity, and what is genuinely unknown.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

namespace gpufleet {

enum class CapabilityKind : std::uint8_t {
  UNKNOWN = 0,
  DISCOVERED = 1,
  VALIDATED = 2,
  INFERRED = 3,
};

constexpr std::string_view to_string(CapabilityKind k) {
  switch (k) {
    case CapabilityKind::DISCOVERED: return "discovered";
    case CapabilityKind::VALIDATED: return "validated";
    case CapabilityKind::INFERRED: return "inferred";
    case CapabilityKind::UNKNOWN:
    default: return "unknown";
  }
}

struct Capability {
  CapabilityId id;
  std::string name;
  CapabilityKind kind = CapabilityKind::UNKNOWN;
  std::string value;        // optional scalar value (e.g. memory bytes, dtype list)
  std::string description;

  bool operator==(const Capability& o) const {
    return id == o.id && name == o.name && kind == o.kind && value == o.value &&
           description == o.description;
  }
  bool operator!=(const Capability& o) const { return !(*this == o); }
};

/// Well-known capability names.
namespace capname {
inline constexpr std::string_view vendor = "vendor";
inline constexpr std::string_view architecture = "architecture";
inline constexpr std::string_view compute_capability = "compute_capability";
inline constexpr std::string_view total_memory = "total_memory";
inline constexpr std::string_view usable_memory = "usable_memory";
inline constexpr std::string_view cuda_available = "cuda_available";
inline constexpr std::string_view runtime = "runtime";
inline constexpr std::string_view dtypes = "dtypes";
inline constexpr std::string_view tensor_cores = "tensor_cores";
inline constexpr std::string_view peer_access = "peer_access";
inline constexpr std::string_view unified_addressing = "unified_addressing";
inline constexpr std::string_view graphs = "graphs";
inline constexpr std::string_view cooperative_launch = "cooperative_launch";
inline constexpr std::string_view kernel_execution = "kernel_execution";
inline constexpr std::string_view memory_roundtrip = "memory_roundtrip";
inline constexpr std::string_view numa = "numa";
inline constexpr std::string_view mig = "mig";
inline constexpr std::string_view driver = "driver";
}  // namespace capname

}  // namespace gpufleet
