#pragma once
// Framed TCP transport.
//
// A thin, blocking Winsock2 wrapper. All framing (the byte-level frame
// enclosure) is handled separately by FrameDecoder/encode_frame; this layer
// only moves raw bytes and reports clean close (recv==0) versus abrupt death
// (recv error / WSAECONNRESET / WSAECONNABORTED). Send is internally
// synchronized so a single stream may be used from the handler thread that is
// also reading, or from a control thread issuing admin commands.
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "gpufleetagent/core/result.hpp"

namespace gpufleet {

// Initialize Winsock. Called once; idempotent.
bool transport_init();

/// A connected TCP stream.
class TcpStream {
 public:
  TcpStream() = default;
  explicit TcpStream(std::uintptr_t sock);  // os socket handle
  ~TcpStream();
  TcpStream(const TcpStream&) = delete;
  TcpStream& operator=(const TcpStream&) = delete;
  TcpStream(TcpStream&& other) noexcept;
  TcpStream& operator=(TcpStream&& other) noexcept;

  static Result<TcpStream> connect(const std::string& host, std::uint16_t port,
                                   int timeout_ms = 5000);

  /// Send all bytes; returns Ok on full send, error otherwise.
  Result<void> send_all(std::span<const std::uint8_t> data);
  /// Receive up to p buf size bytes. Returns the number received; 0 means the
  /// peer closed cleanly. Abrupt reset/abort is reported as an error.
  Result<std::size_t> recv_some(std::span<std::uint8_t> buf);
  /// Like recv_some but distinguishes "no data available right now" (nullopt)
  /// from a clean close (0) and from an error. Uses the current recv timeout.
  Result<std::optional<std::size_t>> recv_some_opt(std::span<std::uint8_t> buf);

  void close();
  bool open() const noexcept;
  void set_timeout(int ms);
  std::string peer_desc() const;
  std::uintptr_t handle() const { return sock_; }

 private:
  std::uintptr_t sock_ = static_cast<std::uintptr_t>(-1);
  mutable bool send_locked_ = false;
};

/// A listening TCP endpoint.
class TcpListener {
 public:
  TcpListener() = default;
  ~TcpListener();
  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;

  static Result<TcpListener> bind(const std::string& host, std::uint16_t port);
  Result<TcpStream> accept(int timeout_ms = 1000);
  void close();
  std::uint16_t port() const { return port_; }

 private:
  std::uintptr_t sock_ = static_cast<std::uintptr_t>(-1);
  std::uint16_t port_ = 0;
};

}  // namespace gpufleet
