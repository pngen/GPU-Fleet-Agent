#include <cstdio>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
int main() {
  std::printf("=== stale authority rejection ===\n");
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  gpufleet::Registration reg;
  reg.worker = gpufleet::WorkerId(10); reg.worker_boot = gpufleet::WorkerBootId(100);
  reg.protocol_version.value = 1u; reg.registration_generation = gpufleet::RegistrationGeneration(1);
  reg.epoch = gpufleet::CoordinatorEpoch(1);
  store.register_worker(reg, 1000);
  gpufleet::Registration stale_epoch = reg; stale_epoch.epoch = gpufleet::CoordinatorEpoch(0);
  auto r1 = store.register_worker(stale_epoch, 1000);
  gpufleet::Registration stale_boot = reg; stale_boot.worker_boot = gpufleet::WorkerBootId(555);
  auto r2 = store.register_worker(stale_boot, 1000);
  std::printf("stale epoch  -> %s\n", r1.ok() ? "accepted" : "rejected");
  std::printf("stale boot   -> %s\n", r2.ok() ? "accepted" : "rejected");
  return 0;
}
