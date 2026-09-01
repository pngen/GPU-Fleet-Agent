#include <cstdio>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/observation.hpp"
int main() {
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  gpufleet::Registration reg;
  reg.worker = gpufleet::WorkerId(10); reg.worker_boot = gpufleet::WorkerBootId(100);
  reg.protocol_version.value = 1u; reg.registration_generation = gpufleet::RegistrationGeneration(1);
  reg.epoch = gpufleet::CoordinatorEpoch(1);
  store.register_worker(reg, 1000);
  std::string key = "nvidia|NVIDIA|GPU-1|0000:01:00.0|blackwell|12.0|34359738368|13040";
  std::printf("=== drain / undrain ===\n");
  std::printf("drain -> %s\n", (store.set_drain(gpufleet::WorkerId(10), key, gpufleet::DrainState::DRAINING, 2000).ok() ? "ok" : "err"));
  std::printf("undrain -> %s\n", (store.set_drain(gpufleet::WorkerId(10), key, gpufleet::DrainState::ACTIVE, 3000).ok() ? "ok" : "err"));
  return 0;
}
