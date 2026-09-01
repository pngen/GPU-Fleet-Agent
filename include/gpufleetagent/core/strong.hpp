#pragma once
// Strong typed identifiers and generations.
//
// GPU Fleet Agent represents every identity and every authority-rolling
// generation as its own C++ type. IDs and generations are deliberately not
// raw integers and not one another: an ID identifies an entity and is stable
// for the entity's lifetime, while a generation encodes an independent
// authority timeline that may roll independently of any other generation.
//
// Convention: a Zero StrongId or Generation value is the null / unset value.
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <type_traits>

namespace gpufleet {

/// A strongly typed identifier. Distinct Tag types produce distinct C++ types.
template <typename Tag, typename Rep = std::uint64_t>
class StrongId {
 public:
  using TagType = Tag;
  using RepType = Rep;

  constexpr StrongId() noexcept : value_(0) {}
  explicit constexpr StrongId(Rep v) noexcept : value_(v) {}
  template <typename R = Rep, typename = std::enable_if_t<std::is_integral_v<R>>>
  explicit constexpr StrongId(int v) noexcept : value_(static_cast<Rep>(v)) {}

  constexpr Rep value() const noexcept { return value_; }
  constexpr bool is_zero() const noexcept { return value_ == 0; }
  constexpr explicit operator bool() const noexcept { return value_ != 0; }

  constexpr bool operator==(const StrongId& o) const noexcept { return value_ == o.value_; }
  constexpr bool operator!=(const StrongId& o) const noexcept { return value_ != o.value_; }
  constexpr bool operator<(const StrongId& o) const noexcept { return value_ < o.value_; }
  constexpr bool operator<=(const StrongId& o) const noexcept { return value_ <= o.value_; }
  constexpr bool operator>(const StrongId& o) const noexcept { return value_ > o.value_; }
  constexpr bool operator>=(const StrongId& o) const noexcept { return value_ >= o.value_; }

  std::string to_string() const { return std::to_string(value_); }

 private:
  Rep value_;
};

template <typename Tag, typename Rep>
std::ostream& operator<<(std::ostream& os, const StrongId<Tag, Rep>& id) {
  return os << id.to_string();
}

/// A strongly typed generation. Generations roll independently per authority
/// domain; a fresh incarnation of an authority *always* observes a strictly
/// greater generation than any prior accepted one.
template <typename Tag, typename Rep = std::uint64_t>
class Generation {
 public:
  using TagType = Tag;
  using RepType = Rep;

  constexpr Generation() noexcept : value_(0) {}
  explicit constexpr Generation(Rep v) noexcept : value_(v) {}
  template <typename R = Rep, typename = std::enable_if_t<std::is_integral_v<R>>>
  explicit constexpr Generation(int v) noexcept : value_(static_cast<Rep>(v)) {}

  constexpr Rep value() const noexcept { return value_; }
  constexpr bool is_zero() const noexcept { return value_ == 0; }

  /// The next generation in this authority domain. Rolls forward.
  constexpr Generation next() const noexcept { return Generation(static_cast<Rep>(value_ + 1)); }

  /// Returns whether this generation is strictly greater than p o.
  constexpr bool after(const Generation& o) const noexcept { return value_ > o.value_; }

  constexpr bool operator==(const Generation& o) const noexcept { return value_ == o.value_; }
  constexpr bool operator!=(const Generation& o) const noexcept { return value_ != o.value_; }
  constexpr bool operator<(const Generation& o) const noexcept { return value_ < o.value_; }
  constexpr bool operator<=(const Generation& o) const noexcept { return value_ <= o.value_; }
  constexpr bool operator>(const Generation& o) const noexcept { return value_ > o.value_; }
  constexpr bool operator>=(const Generation& o) const noexcept { return value_ >= o.value_; }

  std::string to_string() const { return std::to_string(value_); }

 private:
  Rep value_;
};

template <typename Tag, typename Rep>
std::ostream& operator<<(std::ostream& os, const Generation<Tag, Rep>& g) {
  return os << g.to_string();
}

}  // namespace gpufleet

// std::hash specializations.
namespace std {
template <typename Tag, typename Rep>
struct hash<gpufleet::StrongId<Tag, Rep>> {
  size_t operator()(const gpufleet::StrongId<Tag, Rep>& id) const noexcept {
    return std::hash<Rep>()(id.value());
  }
};
template <typename Tag, typename Rep>
struct hash<gpufleet::Generation<Tag, Rep>> {
  size_t operator()(const gpufleet::Generation<Tag, Rep>& g) const noexcept {
    return std::hash<Rep>()(g.value());
  }
};
}  // namespace std
