#pragma once
// GPU Fleet Agent - product and protocol version constants.
#include <cstdint>
#include <string_view>

namespace gpufleet {

inline constexpr std::string_view kProductName = "GPU Fleet Agent";
inline constexpr std::string_view kPublisher = "Summon Software Labs";

inline constexpr std::uint32_t kVersionMajor = 1;
inline constexpr std::uint32_t kVersionMinor = 0;
inline constexpr std::uint32_t kVersionPatch = 0;
inline constexpr std::string_view kVersionString = "1.0.0";

/// The control-protocol version used by the framed TCP wire protocol.
inline constexpr std::uint32_t kProtocolVersion = 1;

/// The on-disk schema version for versioned binary persistence.
inline constexpr std::uint32_t kStoreSchemaVersion = 1;

constexpr std::string_view version_string() noexcept { return kVersionString; }
constexpr std::uint32_t protocol_version() noexcept { return kProtocolVersion; }
constexpr std::uint32_t store_schema_version() noexcept { return kStoreSchemaVersion; }

}  // namespace gpufleet
