#include <cstdio>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
int main() {
  std::printf("=== coordinator epoch rollover ===\n");
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  gpufleet::Registration reg;
  reg.worker = gpufleet::WorkerId(10); reg.worker_boot = gpufleet::WorkerBootId(100);
  reg.protocol_version.value = 1u; reg.registration_generation = gpufleet::RegistrationGeneration(1);
  reg.epoch = gpufleet::CoordinatorEpoch(1);
  store.register_worker(reg, 1000);
  std::printf("epoch before = %s\n", store.epoch().to_string().c_str());
  store.roll_epoch(gpufleet::CoordinatorEpoch(2));
  std::printf("epoch after = %s\n", store.epoch().to_string().c_str());
  // A stale-epoch registration (epoch 1) is now rejected.
  gpufleet::Registration stale = reg; stale.epoch = gpufleet::CoordinatorEpoch(1);
  auto r = store.register_worker(stale, 2000);
  std::printf("stale epoch registration -> %s\n", r.ok() ? "accepted" : "rejected");
  return 0;
}
