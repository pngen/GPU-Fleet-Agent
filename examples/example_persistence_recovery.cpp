#include <cstdio>
#include <string>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/persistence/store.hpp"
int main() {
  const char* path = "example_state.gfle";
  std::remove(path);
  std::printf("=== persistence / recovery ===\n");
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  auto snap = store.snapshot();
  auto r = gpufleet::save_snapshot(path, snap);
  std::printf("save -> %s\n", r.ok() ? "ok" : "err");
  auto l = gpufleet::load_snapshot(path);
  std::printf("load -> %s, digest=%u\n", l.ok() ? "ok" : "err", l.ok() ? gpufleet::snapshot_digest(l.value()) : 0u);
  std::remove(path);
  return 0;
}
