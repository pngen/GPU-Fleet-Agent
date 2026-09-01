#include "tests/test_fw.hpp"
#include "gpufleetagent/core/strong.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

using namespace gpufleet;

int main() {
  // Strong IDs are distinct types, not aliases.
  static_assert(!std::is_same_v<NodeId, WorkerId>);
  static_assert(!std::is_same_v<CoordinatorEpoch, FleetGeneration>);
  static_assert(std::is_same_v<decltype(FleetId{}.value()), std::uint64_t>);

  FleetId a(3u), b(3u), c(4u);
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a < c);
  CHECK_EQ(a.value(), 3u);

  WorkerBootId w1(100u), w2(10u);
  CHECK(w1 != w2);
  CHECK(w1 > w2);
  CHECK(w1.is_zero() == false);
  CHECK(WorkerId{}.is_zero());

  // Generations roll independently.
  CoordinatorEpoch e(5u);
  CHECK(e.next() == CoordinatorEpoch(6u));
  CHECK(e.after(CoordinatorEpoch(4u)));
  CHECK(!e.after(CoordinatorEpoch(5u)));

  FleetGeneration fg(1u);
  // CoordinatorEpoch and FleetGeneration are distinct types and can never be
  // compared; the distinctness is enforced by the type system (above).

  // Generation validity zero is allowed (initial).
  CHECK(CoordinatorEpoch{}.is_zero());

  // to_string.
  CHECK_EQ(FleetId(7u).to_string(), "7");
  CHECK_EQ(CoordinatorEpoch(9u).to_string(), "9");

  return tf::summary("ids");
}
