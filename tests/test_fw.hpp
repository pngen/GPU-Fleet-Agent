#pragma once
// Lightweight test harness. NO timeouts: checks are synchronous and the
// harness never imposes a wall-clock limit. Failures are counted and reported.
#include <cstdio>

namespace tf {
inline int& fails() { static int f = 0; return f; }
inline int& counts() { static int c = 0; return c; }
inline void check(bool ok, const char* expr, const char* file, int line) {
  counts()++;
  if (!ok) { fails()++; std::printf("  FAILED %s:%d  %s\n", file, line, expr); }
}
inline int summary(const char* name) {
  std::printf("[%s] %d checks, %d failures\n", name, counts(), fails());
  return fails() == 0 ? 0 : 1;
}
}  // namespace tf

#define CHECK(cond) tf::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) tf::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
