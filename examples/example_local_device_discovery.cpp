#include <cstdio>
#include "gpufleetagent/device/device_backend.hpp"
#include "gpufleetagent/device/synthetic_backend.hpp"
#include "gpufleetagent/cuda/cuda_backend.hpp"
using namespace gpufleet;
int main() {
  // Local device discovery via the synthetic backend (clearly labeled) and,
  // when available, the real CUDA backend.
  std::printf("=== local device discovery ===\n");
  SyntheticDeviceBackend synth(2, 24ull*1024*1024*1024);
  auto ev = synth.enumerate();
  if (ev.ok()) {
    for (auto& d : ev.value())
      std::printf("synthetic[%d] id=%s cc=%s mem=%llu\n", d.ordinal,
                  d.identity.uuid.value.c_str(), d.identity.compute_capability.sm_string().c_str(),
                  (unsigned long long)d.identity.total_physical_memory);
  }
  auto cuda = make_cuda_backend();
  if (cuda) {
    auto cud = cuda->enumerate();
    if (cud.ok())
      for (auto& d : cud.value())
        std::printf("cuda[%d] pci=%s arch=%s cc=%s mem=%llu\n", d.ordinal,
                    d.identity.pci.to_string().c_str(), d.identity.architecture.c_str(),
                    d.identity.compute_capability.sm_string().c_str(),
                    (unsigned long long)d.identity.total_physical_memory);
  } else {
    std::printf("(no CUDA backend available)\n");
  }
  return 0;
}
