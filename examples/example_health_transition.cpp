#include <cstdio>
#include "gpufleetagent/health/health_state.hpp"
#include "gpufleetagent/health/health_evaluator.hpp"
using namespace gpufleet;
int main() {
  std::printf("=== health transition ===\n");
  HealthSignals ok;
  ok.enumerated = true; ok.device_present = true; ok.was_known = true;
  ok.driver_runtime_ok = true; ok.cuda_init_ok = true;
  ok.memory_alloc_ok = true; ok.kernel_exec_ok = true; ok.sync_ok = true;
  ok.mem_roundtrip_ok = true; ok.last_validation_ok = true;
  std::printf("all ok     => %s\n", std::string(to_string(evaluate_health(ok).state)).c_str());
  ok.observation_stale = true;
  std::printf("going stale=> %s\n", std::string(to_string(evaluate_health(ok).state)).c_str());
  ok.observation_stale = false; ok.consecutive_failures = 5;
  std::printf("5 failures => %s\n", std::string(to_string(evaluate_health(ok).state)).c_str());
  return 0;
}
