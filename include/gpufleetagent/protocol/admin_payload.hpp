#pragma once
// Admin/control payload codecs (device identity, drain target, quarantine).
// Shared by the coordinator and by admin clients (CLI, proof harness).
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/observation/observation.hpp"

namespace gpufleet {

inline std::vector<std::uint8_t> encode_identity_target(const std::string& identity) {
  ByteWriter w;
  w.string(identity, 1024);
  return w.take();
}
inline Result<std::string> decode_identity_target(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  auto s = r.string(1024);
  if (!s.ok()) return error_result(Status::Malformed, "identity target malformed");
  if (!r.done()) return error_result(Status::Malformed, "trailing garbage in identity target");
  return ok_result(s.move_value());
}

struct DrainTarget {
  std::string identity;
  DrainState level = DrainState::ACTIVE;
};

inline std::vector<std::uint8_t> encode_drain_target(const std::string& identity, DrainState level) {
  ByteWriter w;
  w.string(identity, 1024);
  w.u8(static_cast<std::uint8_t>(level));
  return w.take();
}
inline Result<DrainTarget> decode_drain_target(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  auto iden = r.string(1024);
  if (!iden.ok()) return error_result(Status::Malformed, "drain identity malformed");
  std::uint8_t lv;
  if (!r.u8(lv)) return error_result(Status::Malformed, "drain level malformed");
  if (lv > static_cast<std::uint8_t>(DrainState::DRAINED)) {
    return error_result(Status::InvalidEnum, "invalid drain state enum");
  }
  if (!r.done()) return error_result(Status::Malformed, "trailing garbage in drain target");
  DrainTarget t{iden.move_value(), static_cast<DrainState>(lv)};
  return ok_result(std::move(t));
}

struct QuarantineTarget {
  std::string identity;
  std::string reason;
  std::string source;
  DeviceGeneration generation;
};

inline std::vector<std::uint8_t> encode_quarantine(const std::string& identity,
                                                   const std::string& reason,
                                                   const std::string& source,
                                                   DeviceGeneration gen) {
  ByteWriter w;
  w.string(identity, 1024);
  w.string(reason, 512);
  w.string(source, 128);
  w.u64(gen.value());
  return w.take();
}
inline Result<QuarantineTarget> decode_quarantine(std::span<const std::uint8_t> data) {
  ByteReader r(data);
  auto iden = r.string(1024); if (!iden.ok()) return error_result(Status::Malformed, "identity malformed");
  auto rs = r.string(512); if (!rs.ok()) return error_result(Status::Malformed, "reason malformed");
  auto src = r.string(128); if (!src.ok()) return error_result(Status::Malformed, "source malformed");
  std::uint64_t g; if (!r.u64(g)) return error_result(Status::Malformed, "generation malformed");
  if (!r.done()) return error_result(Status::Malformed, "trailing garbage in quarantine");
  QuarantineTarget q{iden.move_value(), rs.move_value(), src.move_value(), DeviceGeneration(g)};
  return ok_result(std::move(q));
}

}  // namespace gpufleet
