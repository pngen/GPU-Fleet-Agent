#include <cstdio>
#include "gpufleetagent/device/synthetic_backend.hpp"
using namespace gpufleet;
int main() {
  SyntheticDeviceBackend backend(1, 32ull*1024*1024*1024);
  auto ev = backend.enumerate();
  if (!ev.ok()) return 1;
  auto probe = backend.probe(ev.value()[0]);
  if (!probe.ok()) return 1;
  std::printf("=== capability inspection ===\n");
  for (auto& c : probe.value().capabilities)
    std::printf("%-20s = %-8s [%s]\n", c.name.c_str(), c.value.c_str(), std::string(to_string(c.kind)).c_str());
  return 0;
}
