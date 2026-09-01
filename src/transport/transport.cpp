#include "gpufleetagent/transport/transport.hpp"

// Winsock2 + ws2tcpip. NOMINMAX / WIN32_LEAN_AND_MEAN are set by the build.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace gpufleet {

namespace {
struct WinsockGuard {
  WinsockGuard() {
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
  }
};
WinsockGuard g_winsock;
std::atomic<bool> g_initialized{false};

inline std::string wsa_error_str(int code) {
  char buf[512];
  // FormatMessageW is heavier than needed; use a numeric description.
  std::snprintf(buf, sizeof(buf), "winsock error %d", code);
  return std::string(buf);
}

Result<TcpStream> wrap_socket(SOCKET s) {
  if (s == INVALID_SOCKET) return error_result(Status::Io, "invalid socket");
  return ok_result(TcpStream(static_cast<std::uintptr_t>(s)));
}
}  // namespace

bool transport_init() {
  bool expected = false;
  if (g_initialized.compare_exchange_strong(expected, true)) {
    // ensure guard constructed
    (void)g_winsock;
  }
  return true;
}

TcpStream::TcpStream(std::uintptr_t sock) : sock_(sock) {}

TcpStream::~TcpStream() { close(); }

TcpStream::TcpStream(TcpStream&& other) noexcept : sock_(other.sock_) {
  other.sock_ = static_cast<std::uintptr_t>(-1);
}

TcpStream& TcpStream::operator=(TcpStream&& other) noexcept {
  if (this != &other) {
    close();
    sock_ = other.sock_;
    other.sock_ = static_cast<std::uintptr_t>(-1);
  }
  return *this;
}

Result<TcpStream> TcpStream::connect(const std::string& host, std::uint16_t port,
                                     int timeout_ms) {
  transport_init();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return error_result(Status::Io, wsa_error_str(WSAGetLastError()));

  struct addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  char portstr[16];
  std::snprintf(portstr, sizeof(portstr), "%u", port);
  if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0 || res == nullptr) {
    closesocket(s);
    return error_result(Status::Io, "getaddrinfo failed");
  }

  // Connect with timeout via non-blocking mode + select.
  u_long nbio = 1;
  ioctlsocket(s, FIONBIO, &nbio);
  int rc = ::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen));
  freeaddrinfo(res);

  if (rc != 0) {
    int err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK) {
      closesocket(s);
      return error_result(Status::Io, wsa_error_str(err));
    }
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int sel = select(0, nullptr, &wfds, nullptr, &tv);
    if (sel <= 0) {
      closesocket(s);
      return error_result(Status::Io, "connect timeout");
    }
    int soerr = 0;
    int len = sizeof(soerr);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &len);
    if (soerr != 0) {
      closesocket(s);
      return error_result(Status::Io, wsa_error_str(soerr));
    }
  }
  nbio = 0;
  ioctlsocket(s, FIONBIO, &nbio);

  // Set send/recv timeouts so a blocked recv can be observed for liveness.
  DWORD tv = static_cast<DWORD>(timeout_ms);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

  return wrap_socket(s);
}

Result<void> TcpStream::send_all(std::span<const std::uint8_t> data) {
  if (sock_ == static_cast<std::uintptr_t>(-1)) return error_result(Status::Io, "socket closed");
  const char* p = reinterpret_cast<const char*>(data.data());
  std::size_t n = data.size();
  while (n > 0) {
    int sent = ::send(static_cast<SOCKET>(sock_), p, static_cast<int>(n > 0x7FFFFFFF ? 0x7FFFFFFF : n), 0);
    if (sent == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      return error_result(Status::Io, wsa_error_str(err));
    }
    n -= static_cast<std::size_t>(sent);
    p += sent;
  }
  return ok_result();
}

Result<std::size_t> TcpStream::recv_some(std::span<std::uint8_t> buf) {
  if (sock_ == static_cast<std::uintptr_t>(-1)) return error_result(Status::Io, "socket closed");
  int rc = ::recv(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf.data()),
                  static_cast<int>(buf.size()), 0);
  if (rc == 0) return ok_result(std::size_t(0));  // clean close
  if (rc == SOCKET_ERROR) {
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
      // No data available right now; not a close. Return a sentinel via error?
      // We use WSAETIMEDOUT as "no data". Distinguish via error code.
      return error_result(Status::Io, "timeout");
    }
    return error_result(Status::Io, wsa_error_str(err));
  }
  return ok_result(std::size_t(rc));
}

Result<std::optional<std::size_t>> TcpStream::recv_some_opt(std::span<std::uint8_t> buf) {
  if (sock_ == static_cast<std::uintptr_t>(-1)) return error_result(Status::Io, "socket closed");
  int rc = ::recv(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf.data()),
                  static_cast<int>(buf.size()), 0);
  if (rc == 0) return ok_result(std::optional<std::size_t>(std::size_t(0)));
  if (rc == SOCKET_ERROR) {
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
      return ok_result(std::optional<std::size_t>());
    }
    return error_result(Status::Io, wsa_error_str(err));
  }
  return ok_result(std::optional<std::size_t>(static_cast<std::size_t>(rc)));
}

void TcpStream::set_timeout(int ms) {
  if (sock_ == static_cast<std::uintptr_t>(-1)) return;
  DWORD tv = static_cast<DWORD>(ms);
  setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}

void TcpStream::close() {
  if (sock_ != static_cast<std::uintptr_t>(-1)) {
    ::closesocket(static_cast<SOCKET>(sock_));
    sock_ = static_cast<std::uintptr_t>(-1);
  }
}

bool TcpStream::open() const noexcept { return sock_ != static_cast<std::uintptr_t>(-1); }

std::string TcpStream::peer_desc() const {
  if (sock_ == static_cast<std::uintptr_t>(-1)) return "(closed)";
  sockaddr_in addr{};
  int len = sizeof(addr);
  getpeername(static_cast<SOCKET>(sock_), reinterpret_cast<sockaddr*>(&addr), &len);
  char buf[64];
  inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
  return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
}

TcpListener::~TcpListener() { close(); }
TcpListener::TcpListener(TcpListener&& other) noexcept : sock_(other.sock_), port_(other.port_) {
  other.sock_ = static_cast<std::uintptr_t>(-1);
  other.port_ = 0;
}
TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this != &other) {
    close();
    sock_ = other.sock_;
    port_ = other.port_;
    other.sock_ = static_cast<std::uintptr_t>(-1);
    other.port_ = 0;
  }
  return *this;
}

Result<TcpListener> TcpListener::bind(const std::string& host, std::uint16_t port) {
  transport_init();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return error_result(Status::Io, wsa_error_str(WSAGetLastError()));

  BOOL reuse = TRUE;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  struct addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  struct addrinfo* res = nullptr;
  char portstr[16];
  std::snprintf(portstr, sizeof(portstr), "%u", port);
  if (getaddrinfo(host.empty() ? nullptr : host.c_str(), portstr, &hints, &res) != 0 || res == nullptr) {
    closesocket(s);
    return error_result(Status::Io, "getaddrinfo failed");
  }
  int rc = ::bind(s, res->ai_addr, static_cast<int>(res->ai_addrlen));
  freeaddrinfo(res);
  if (rc != 0) {
    closesocket(s);
    return error_result(Status::Io, wsa_error_str(WSAGetLastError()));
  }
  // Determine actual port (0 => ephemeral).
  sockaddr_in actual{};
  int alen = sizeof(actual);
  getsockname(s, reinterpret_cast<sockaddr*>(&actual), &alen);
  std::uint16_t actual_port = ntohs(actual.sin_port);

  if (::listen(s, SOMAXCONN) != 0) {
    closesocket(s);
    return error_result(Status::Io, wsa_error_str(WSAGetLastError()));
  }

  TcpListener l;
  l.sock_ = static_cast<std::uintptr_t>(s);
  l.port_ = actual_port;
  return ok_result(std::move(l));
}

Result<TcpStream> TcpListener::accept(int timeout_ms) {
  if (sock_ == static_cast<std::uintptr_t>(-1)) return error_result(Status::Io, "listener closed");
  // Non-blocking accept with select for timeout.
  u_long nbio = 1;
  ioctlsocket(static_cast<SOCKET>(sock_), FIONBIO, &nbio);
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(static_cast<SOCKET>(sock_), &rfds);
  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int sel = select(0, &rfds, nullptr, nullptr, &tv);
  if (sel <= 0) {
    nbio = 0;
    ioctlsocket(static_cast<SOCKET>(sock_), FIONBIO, &nbio);
    return error_result(Status::Io, "accept timeout");
  }
  SOCKET c = ::accept(static_cast<SOCKET>(sock_), nullptr, nullptr);
  nbio = 0;
  ioctlsocket(static_cast<SOCKET>(sock_), FIONBIO, &nbio);
  if (c == INVALID_SOCKET) return error_result(Status::Io, wsa_error_str(WSAGetLastError()));
  DWORD dtv = static_cast<DWORD>(timeout_ms);
  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&dtv), sizeof(dtv));
  setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&dtv), sizeof(dtv));
  return wrap_socket(c);
}

void TcpListener::close() {
  if (sock_ != static_cast<std::uintptr_t>(-1)) {
    ::closesocket(static_cast<SOCKET>(sock_));
    sock_ = static_cast<std::uintptr_t>(-1);
  }
}

}  // namespace gpufleet
