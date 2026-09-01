#pragma once
// Versioned binary persistence.
//
// On-disk layout (big-endian, fixed byte order):
//   u32 magic "GFLE"  (0x47464C45)
//   u32 schema version
//   u64 payload length
//   u8[] payload    (a snapshot blob produced by encode_snapshot)
//   u32 CRC-32 over magic..payload (excluding the trailing CRC field)
//
// Writes are atomic (temp file + rename). Loads reject: bad magic, unsupported
// schema version, truncated payload, CRC mismatch, and trailing garbage. The
// recovered snapshot's dynamic observations are NOT treated as fresh; the
// coordinator re-applies freshness on recovery.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/core/result.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/version.hpp"

namespace gpufleet {

inline constexpr std::uint32_t kStoreMagic = 0x47464C45u;  // "GFLE"

/// Deterministic digest (CRC-32) of the canonical snapshot payload. Used by
/// the recovery proof to compare a reconstructed state to a persisted state.
inline std::uint32_t snapshot_digest(const StateSnapshot& s) {
  auto body = encode_snapshot(s);
  return crc32(body);
}

/// Save p snapshot to p path atomically.
inline Result<void> save_snapshot(const std::string& path, const StateSnapshot& s) {
  auto body = encode_snapshot(s);

  ByteWriter w;
  w.u32(kStoreMagic);
  w.u32(store_schema_version());
  w.u64(static_cast<std::uint64_t>(body.size()));
  w.raw_bytes(body);
  std::uint32_t crc = crc32(w.data());
  ByteWriter cr;
  cr.u32(crc);
  auto bytes = w.take();
  bytes.insert(bytes.end(), cr.data().begin(), cr.data().end());

  std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return error_result(Status::Io, "cannot open temp file for write");
  std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
  int flush = std::fflush(f);
  int close = std::fclose(f);
  if (written != bytes.size() || flush != 0 || close != 0) {
    std::remove(tmp.c_str());
    return error_result(Status::Io, "failed to write store file");
  }
  // Atomic replacement.
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    // On Windows, rename fails if destination exists; use remove+rename best effort.
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
      std::remove(tmp.c_str());
      return error_result(Status::Io, "failed to atomically replace store file");
    }
  }
  return ok_result();
}

/// Load a snapshot from p path, rejecting corruption/truncation/garbage.
inline Result<StateSnapshot> load_snapshot(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return error_result(Status::Io, "cannot open store file");
  std::fseek(f, 0, SEEK_END);
  long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz < 0) { std::fclose(f); return error_result(Status::Io, "cannot size store file"); }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
  if (sz > 0) {
    std::size_t rd = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (rd != bytes.size()) return error_result(Status::Truncated, "store file truncated on read");
  } else {
    std::fclose(f);
    return error_result(Status::Truncated, "store file empty");
  }

  if (bytes.size() < 4 + 4 + 8 + 4) {
    return error_result(Status::Truncated, "store file too small");
  }
  ByteReader r(bytes);
  std::uint32_t magic = 0, ver = 0;
  std::uint64_t len = 0;
  if (!r.u32(magic)) return error_result(Status::Truncated, "magic truncated");
  if (!r.u32(ver)) return error_result(Status::Truncated, "version truncated");
  if (!r.u64(len)) return error_result(Status::Truncated, "length truncated");
  if (magic != kStoreMagic) return error_result(Status::Corrupt, "bad store magic");
  if (ver != store_schema_version()) return error_result(Status::VersionMismatch, "unsupported schema version");
  if (len > (1ull << 31)) return error_result(Status::OutOfBounds, "payload too large");

  if (bytes.size() != 4 + 4 + 8 + static_cast<std::size_t>(len) + 4) {
    return error_result(Status::Truncated, "store file length and size mismatch (truncation or trailing garbage)");
  }

  // CRC over magic..payload.
  std::uint32_t stored_crc = 0;
  ByteReader crc_r(std::span<const std::uint8_t>(bytes.data() + (4 + 4 + 8 + len), 4));
  if (!crc_r.u32(stored_crc)) return error_result(Status::Truncated, "crc truncated");
  std::uint32_t calc = crc32(std::span<const std::uint8_t>(bytes.data(), bytes.size() - 4));
  if (calc != stored_crc) return error_result(Status::Corrupt, "store CRC mismatch");

  std::span<const std::uint8_t> payload(bytes.data() + (4 + 4 + 8), static_cast<std::size_t>(len));
  return decode_snapshot(payload);
}

}  // namespace gpufleet
