#pragma once
// Lightweight status/result plumbing used throughout the runtime.
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace gpufleet {

enum class Status : std::uint8_t {
  Ok = 0,
  Corrupt,
  Truncated,
  OutOfBounds,
  InvalidEnum,
  InvalidGeneration,
  ProtocolMismatch,
  VersionMismatch,
  Duplicate,
  UnknownMessage,
  Malformed,
  Stale,
  NotAuthoritative,
  NotFound,
  AlreadyExists,
  Quarantined,
  Drained,
  Unsupported,
  Io,
  IdentityMismatch,
  CapacityExceeded,
  Rejected,
  Internal,
};

const char* to_string(Status s) noexcept;

/// A structured error with a stable code and a human/JSON message.
struct Error {
  Status code = Status::Internal;
  std::string message;
};

/// Result<T> is either a value or an Error. Use it instead of exceptions for
/// the hot, deterministic paths (codec, protocol, persistence, eligibility).
template <typename T>
class Result {
 public:
  Result(T value) : storage_(std::move(value)) {}                // NOLINT
  Result(Error err) : storage_(std::move(err)) {}                // NOLINT
  Result(Status code, std::string msg) : storage_(Error{code, std::move(msg)}) {}

  bool ok() const noexcept { return std::holds_alternative<T>(storage_); }
  bool has_value() const noexcept { return ok(); }
  explicit operator bool() const noexcept { return ok(); }

  const T& value() const { return std::get<T>(storage_); }
  T& value() { return std::get<T>(storage_); }
  T&& move_value() { return std::move(std::get<T>(storage_)); }

  const Error& error() const { return std::get<Error>(storage_); }

 private:
  std::variant<T, Error> storage_;
};

// Result<void> specialization.
template <>
class Result<void> {
 public:
  Result() = default;                                           // NOLINT
  Result(Error err) : error_(std::move(err)) {}                 // NOLINT
  Result(Status code, std::string msg) : error_(Error{code, std::move(msg)}) {}

  bool ok() const noexcept { return !has_error_; }
  bool has_value() const noexcept { return ok(); }
  explicit operator bool() const noexcept { return ok(); }
  const Error& error() const { return error_; }

 private:
  bool has_error_ = false;
  Error error_{};
};

// Convenience makers.
template <typename T>
Result<T> ok_result(T v) { return Result<T>(std::move(v)); }

inline Result<void> ok_result() { return Result<void>(); }

inline Error error_result(Status code, std::string msg) {
  return Error{code, std::move(msg)};
}

}  // namespace gpufleet
