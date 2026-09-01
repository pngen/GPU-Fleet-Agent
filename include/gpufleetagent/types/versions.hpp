#pragma once
// Version value types. These are distinct, strong types so that e.g. an agent
// version can never be accidentally compared to a protocol version.
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>

namespace gpufleet {

/// A semantic version (major.minor.patch).
struct SemanticVersion {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;

  constexpr bool operator==(const SemanticVersion& o) const noexcept {
    return major == o.major && minor == o.minor && patch == o.patch;
  }
  constexpr bool operator!=(const SemanticVersion& o) const noexcept { return !(*this == o); }
  constexpr bool operator<(const SemanticVersion& o) const noexcept {
    if (major != o.major) return major < o.major;
    if (minor != o.minor) return minor < o.minor;
    return patch < o.patch;
  }
  constexpr bool operator>(const SemanticVersion& o) const noexcept { return o < *this; }

  std::string to_string() const { return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch); }
};

/// The agent software version.
struct AgentVersion {
  SemanticVersion version;
  std::string to_string() const { return version.to_string(); }
};

/// The version of the framed control protocol. A mismatched value is rejected.
struct ProtocolVersion {
  std::uint32_t value = 0;
};

/// Driver version, opaque text as reported by the platform (e.g. CUDA driver).
struct DriverVersion {
  std::string text;
  bool empty() const { return text.empty(); }
};

/// CUDA runtime version.
struct RuntimeVersion {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::string to_string() const { return std::to_string(major) + "." + std::to_string(minor); }
};

}  // namespace gpufleet
