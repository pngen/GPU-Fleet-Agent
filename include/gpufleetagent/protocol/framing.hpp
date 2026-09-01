#pragma once
// Deterministic bounded binary framing.
//
// Frame layout (all integers big-endian / network order):
//   u32  magic "GFLA"  (0x47464C41)
//   u16  protocol version
//   u8   message type
//   u32  payload length   (bounded to kMaxPayloadLength)
//   u8[] payload
//   u32  CRC-32 over magic..payload (excluding the trailing CRC field)
//
// The decoder is stateful and incremental so a stream can be fed in arbitrary
// chunk sizes (e.g. TCP reads) and correctly handle half-written frames. It
// rejects: unknown version, invalid enum message type, oversized payloads,
// truncated payloads, bad magic, and CRC mismatches. Leftover bytes at stream
// close are reported as trailing-garbage / truncation by the transport.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gpufleetagent/core/codec.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/version.hpp"

namespace gpufleet {

inline constexpr std::uint32_t kFrameMagic = 0x47464C41u;  // "GFLA"
inline constexpr std::uint32_t kMaxPayloadLength = 16u * 1024u * 1024u;  // 16 MiB
inline constexpr std::size_t kFrameHeader = 4 + 2 + 1 + 4;                // magic+ver+type+len
inline constexpr std::size_t kFrameTrailer = 4;                           // crc

struct Frame {
  MessageType type = MessageType::HELLO;
  std::vector<std::uint8_t> payload;
};

/// Encodes one complete frame. p type must be a valid message type.
inline std::vector<std::uint8_t> encode_frame(MessageType type,
                                              std::span<const std::uint8_t> payload) {
  ByteWriter w;
  w.u32(kFrameMagic);
  w.u16(static_cast<std::uint16_t>(protocol_version()));
  w.u8(static_cast<std::uint8_t>(type));
  w.u32(static_cast<std::uint32_t>(payload.size()));
  w.raw_bytes(payload);
  std::uint32_t crc = crc32(w.data());
  // append crc (big-endian) into a temp and then append to the frame.
  ByteWriter c;
  c.u32(crc);
  std::vector<std::uint8_t> out = w.take();
  out.insert(out.end(), c.data().begin(), c.data().end());
  return out;
}

/// Incremental, stateful frame decoder.
class FrameDecoder {
 public:
  enum class Result { NeedMore = 0, FrameReady = 1, Error = 2 };

  /// Feed bytes into the decoder. On FrameReady, p out holds the decoded
  /// frame and the consumed bytes are removed from the internal buffer.
  Result feed(std::span<const std::uint8_t> data, Frame& out, std::string& error) {
    buf_.insert(buf_.end(), data.begin(), data.end());

    if (buf_.size() < kFrameHeader + kFrameTrailer) {
      return Result::NeedMore;
    }

    ByteReader head(buf_);
    std::uint32_t magic = 0, payload_len = 0;
    std::uint16_t ver = 0;
    std::uint8_t type = 0;
    if (!head.u32(magic)) { error = "frame header truncated"; return Result::Error; }
    if (!head.u16(ver)) { error = "frame header truncated"; return Result::Error; }
    if (!head.u8(type)) { error = "frame header truncated"; return Result::Error; }
    if (!head.u32(payload_len)) { error = "frame header truncated"; return Result::Error; }

    if (magic != kFrameMagic) { error = "bad frame magic"; return Result::Error; }
    if (ver != protocol_version()) { error = "unknown protocol version"; return Result::Error; }
    if (!valid_message_type(type)) { error = "invalid enum message type"; return Result::Error; }
    if (payload_len > kMaxPayloadLength) { error = "oversized payload"; return Result::Error; }

    const std::size_t total = kFrameHeader + payload_len + kFrameTrailer;
    if (buf_.size() < total) {
      // A declared payload_len we cannot yet satisfy will eventually be either
      // completed or reported as truncation at close. If it exceeds the bound
      // already handled above, this is a genuine not-enough-data condition.
      return Result::NeedMore;
    }

    // Validate CRC over magic..payload (everything except trailing 4 bytes).
    std::uint32_t stored_crc = 0;
    ByteReader crc_reader(std::span<const std::uint8_t>(buf_.data() + (total - 4), 4));
    if (!crc_reader.u32(stored_crc)) { error = "frame crc truncated"; return Result::Error; }
    std::uint32_t calc_crc = crc32(std::span<const std::uint8_t>(buf_.data(), total - 4));
    if (calc_crc != stored_crc) { error = "frame crc mismatch"; return Result::Error; }

    out.type = static_cast<MessageType>(type);
    out.payload.assign(buf_.begin() + kFrameHeader,
                       buf_.begin() + kFrameHeader + static_cast<std::ptrdiff_t>(payload_len));

    buf_.erase(buf_.begin(),
               buf_.begin() + static_cast<std::ptrdiff_t>(total));
    return Result::FrameReady;
  }

  /// Number of buffered bytes not yet consumed into a frame.
  std::size_t buffered() const noexcept { return buf_.size(); }
  void reset() { buf_.clear(); }

 private:
  std::vector<std::uint8_t> buf_;
};

}  // namespace gpufleet
