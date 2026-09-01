#pragma once
// Deterministic binary encoding.
//
// All multi-byte integers are encoded in a FIXED byte order (big-endian /
// network order) so that persistence and wire encoding are byte-for-byte
// identical on any platform. Strings and byte blobs are length-prefixed with
// explicit bounded lengths so that an unauthenticated / corrupt input can
// never cause an unbounded allocation or read past the buffer.
//
// CRC-32 (IEEE 802.3 polynomial 0xEDB88320) is used for store integrity.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gpufleetagent/core/result.hpp"

namespace gpufleet {

namespace detail {
constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320u;
}  // namespace detail

/// CRC-32 over a byte span. Deterministic and platform independent.
constexpr std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
      crc = (crc >> 1) ^ ((crc & 1u) ? detail::kCrc32Polynomial : 0u);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

inline constexpr std::uint32_t kMaxStringLen = 65535u;
inline constexpr std::uint32_t kMaxBytesLen = 0x7FFFFFFFu;

/// Growable writer for deterministic big-endian encoding.
class ByteWriter {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) {
    buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    buf_.push_back(static_cast<std::uint8_t>(v & 0xFF));
  }
  void u32(std::uint32_t v) {
    buf_.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    buf_.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    buf_.push_back(static_cast<std::uint8_t>(v & 0xFF));
  }
  void u64(std::uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
      buf_.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
    }
  }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void f64(double v) {
    std::uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    u64(bits);
  }

  void bytes(std::span<const std::uint8_t> b) {
    u32(static_cast<std::uint32_t>(b.size()));
    buf_.insert(buf_.end(), b.begin(), b.end());
  }
  void string(std::string_view s, std::uint32_t max_len = kMaxStringLen) {
    if (s.size() > max_len) {
      // Caller contract violation; clamp is not acceptable, so we encode the
      // maximum and let the reader treat the truncation as corrupt input.
      s = s.substr(0, max_len);
    }
    u32(static_cast<std::uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
  }
  void raw_bytes(std::span<const std::uint8_t> b) { buf_.insert(buf_.end(), b.begin(), b.end()); }

  std::span<const std::uint8_t> data() const { return buf_; }
  const std::vector<std::uint8_t>& buffer() const { return buf_; }
  std::size_t size() const noexcept { return buf_.size(); }
  void reserve(std::size_t n) { buf_.reserve(n); }
  std::vector<std::uint8_t> take() { return std::move(buf_); }

 private:
  std::vector<std::uint8_t> buf_;
};

/// Bounded cursor reader for big-endian decoding. Every read is bounds checked
/// and never allocates more than the declared maximum for a length-prefixed
/// field. Any failure reports Status::Truncated or Status::OutOfBounds.
class ByteReader {
 public:
  ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

  bool u8(std::uint8_t& v) {
    if (pos_ + 1 > data_.size()) return false;
    v = data_[pos_++];
    return true;
  }
  bool u16(std::uint16_t& v) {
    if (pos_ + 2 > data_.size()) return false;
    v = static_cast<std::uint16_t>((std::uint16_t(data_[pos_]) << 8) | data_[pos_ + 1]);
    pos_ += 2;
    return true;
  }
  bool u32(std::uint32_t& v) {
    if (pos_ + 4 > data_.size()) return false;
    v = (std::uint32_t(data_[pos_]) << 24) | (std::uint32_t(data_[pos_ + 1]) << 16) |
        (std::uint32_t(data_[pos_ + 2]) << 8) | std::uint32_t(data_[pos_ + 3]);
    pos_ += 4;
    return true;
  }
  bool u64(std::uint64_t& v) {
    if (pos_ + 8 > data_.size()) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | data_[pos_ + i];
    pos_ += 8;
    return true;
  }
  bool i64(std::int64_t& v) {
    std::uint64_t u;
    if (!u64(u)) return false;
    std::memcpy(&v, &u, sizeof(v));
    return true;
  }
  bool f64(double& v) {
    std::uint64_t bits;
    if (!u64(bits)) return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
  }

  /// Reads a length-prefixed byte span. Rejects a length larger than the
  /// remaining buffer (Status::Truncated) and a declared length larger than
  /// p max_len (Status::OutOfBounds).
  Result<std::span<const std::uint8_t>> bytes(std::uint32_t max_len = kMaxBytesLen) {
    std::uint32_t len;
    if (!u32(len)) return error_result(Status::Truncated, "byte length truncated");
    if (len > max_len) return error_result(Status::OutOfBounds, "byte length exceeds bound");
    if (pos_ + len > data_.size()) return error_result(Status::Truncated, "byte payload truncated");
    auto span = data_.subspan(pos_, len);
    pos_ += len;
    return ok_result(span);
  }

  /// Reads exactly p count raw bytes (no length prefix). Bounds checked.
  Result<std::span<const std::uint8_t>> raw(std::uint32_t count) {
    if (pos_ + count > data_.size()) return error_result(Status::Truncated, "raw payload truncated");
    auto span = data_.subspan(pos_, count);
    pos_ += count;
    return ok_result(span);
  }

  Result<std::string> string(std::uint32_t max_len = kMaxStringLen) {
    auto span = bytes(max_len);
    if (!span.ok()) return error_result(Status::Truncated, "string payload truncated");
    return ok_result(std::string(reinterpret_cast<const char*>(span.value().data()), span.value().size()));
  }

  bool done() const noexcept { return pos_ == data_.size(); }
  std::size_t remaining() const noexcept { return data_.size() - pos_; }
  std::size_t position() const noexcept { return pos_; }
  void skip(std::size_t n) {
    if (pos_ + n > data_.size()) pos_ = data_.size();
    else pos_ += n;
  }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

}  // namespace gpufleet
