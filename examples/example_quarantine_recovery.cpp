#include <cstdio>
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
int main() {
  gpufleet::FleetStateStore store(gpufleet::CoordinatorId(1), gpufleet::CoordinatorEpoch(1));
  std::string key = "nvidia|NVIDIA|GPU-1|0000:01:00.0|blackwell|12.0|34359738368|13040";
  std::printf("=== quarantine / recovery ===\n");
  gpufleet::QuarantineRecord q; q.reason = "repeated validation failure"; q.source = "operator";
  q.generation = gpufleet::DeviceGeneration(1); q.at = 2000; q.authority = gpufleet::CoordinatorEpoch(1);
  std::printf("quarantine -> %s\n", (store.set_quarantine(gpufleet::WorkerId(10), key, q, 2000).ok() ? "ok" : "err"));
  std::printf("clear -> %s\n", (store.clear_quarantine(gpufleet::WorkerId(10), key, gpufleet::CoordinatorEpoch(1), 3000).ok() ? "ok" : "err"));
  return 0;
}
