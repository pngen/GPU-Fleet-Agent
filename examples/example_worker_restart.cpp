#include <cstdio>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
int main() {
  std::printf("=== worker restart (WorkerBootId fencing) ===\n");
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  gpufleet::Registration reg;
  reg.worker = gpufleet::WorkerId(10); reg.worker_boot = gpufleet::WorkerBootId(100);
  reg.protocol_version.value = 1u; reg.registration_generation = gpufleet::RegistrationGeneration(1);
  reg.epoch = gpufleet::CoordinatorEpoch(1);
  store.register_worker(reg, 1000);
  std::printf("boot after start = %s\n", store.snapshot().workers.at(gpufleet::WorkerId(10)).boot.to_string().c_str());
  // Re-register the SAME boot with the SAME generation (already seen) -> rejected (stale registration generation)
  gpufleet::Registration re = reg;
  auto r = store.register_worker(re, 1100);
  std::printf("stale re-register same gen -> %s\n", r.ok() ? "accepted" : "rejected");
  // Mark lost, then a fresh boot supersedes.
  store.mark_worker_lost(gpufleet::WorkerId(10), 1200);
  gpufleet::Registration re2 = reg; re2.worker_boot = gpufleet::WorkerBootId(999); re2.registration_generation = gpufleet::RegistrationGeneration(2);
  store.register_worker(re2, 1300);
  std::printf("fresh boot after restart = %s (old boot is fenced)\n", store.snapshot().workers.at(gpufleet::WorkerId(10)).boot.to_string().c_str());
  return 0;
}
