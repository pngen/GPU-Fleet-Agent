#pragma once
// Clock and timestamp primitives.
//
// A Timestamp is a point in time measured in milliseconds since the Unix epoch
// (1970-01-01T00:00:00Z). Dynamic observations carry a Timestamp and a
// freshness threshold; freshness is always evaluated against a Clock so that
// tests can drive time deterministically instead of sleeping.
#include <chrono>
#include <cstdint>

namespace gpufleet {

using Timestamp = std::int64_t;  // milliseconds since Unix epoch.
using DurationMs = std::int64_t;

inline constexpr Timestamp kInvalidTimestamp = 0;

/// Returns the current wall-clock time in milliseconds since Unix epoch.
inline Timestamp now_millis() noexcept {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/// Injectable clock. Tests substitute a DeterministicClock to drive time.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual Timestamp now() const noexcept = 0;
};

/// Real wall-clock.
class SystemClock final : public Clock {
 public:
  Timestamp now() const noexcept override { return now_millis(); }
};

/// Deterministic, manually-advanced clock for tests.
class DeterministicClock final : public Clock {
 public:
  explicit DeterministicClock(Timestamp t = kInvalidTimestamp) : t_(t) {}
  Timestamp now() const noexcept override { return t_; }
  void set(Timestamp t) noexcept { t_ = t; }
  void advance(DurationMs ms) noexcept { t_ += ms; }
  Timestamp current() const noexcept { return t_; }

 private:
  Timestamp t_;
};

}  // namespace gpufleet
