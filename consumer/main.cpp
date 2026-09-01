// Downstream consumer: demonstrates find_package(GPUFleetAgent CONFIG REQUIRED).
#include <cstdio>
#include "gpufleetagent/version.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/observation/observation.hpp"

using namespace gpufleet;

int main() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  Registration reg;
  reg.worker = WorkerId(42u);
  reg.worker_boot = WorkerBootId(7u);
  reg.protocol_version.value = protocol_version();
  reg.registration_generation = RegistrationGeneration(1u);
  reg.epoch = CoordinatorEpoch(1u);
  auto r = store.register_worker(reg, 1000);
  if (!r.ok()) { std::printf("register failed: %s\n", r.error().message.c_str()); return 1; }
  auto snap = store.snapshot();
  std::printf("consumer ok: workers=%zu devices=%zu epoch=%s\n",
              snap.workers.size(), snap.devices.size(), snap.epoch.to_string().c_str());
  return 0;
}
