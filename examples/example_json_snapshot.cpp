#include <cstdio>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/fleet/registration.hpp"
int main() {
  std::printf("=== JSON snapshot ===\n");
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  auto snap = store.snapshot();
  auto blob = gpufleet::encode_snapshot(snap);
  // human/json summary
  std::printf("{\"epoch\":%s,\"workers\":%llu,\"devices\":%llu}\n",
              snap.epoch.to_string().c_str(), (unsigned long long)snap.workers.size(),
              (unsigned long long)snap.devices.size());
  return 0;
}
