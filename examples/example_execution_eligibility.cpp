#include <cstdio>
#include "gpufleetagent/eligibility/eligibility.hpp"
#include "gpufleetagent/eligibility/eligibility_evaluator.hpp"
using namespace gpufleet;
int main() {
  std::printf("=== execution eligibility ===\n");
  EligibilityInput in;
  in.health = HealthState::HEALTHY; in.observation_fresh = true; in.cuda_available = true;
  in.device_present = true; in.sufficient_memory = true; in.available_memory = 24ull*1024*1024*1024;
  auto r = evaluate_eligibility(in);
  std::printf("fresh+healthy => %s\n", std::string(to_string(r.state)).c_str());
  // now stale
  in.observation_fresh = false;
  r = evaluate_eligibility(in);
  std::printf("stale         => %s reasons=[", std::string(to_string(r.state)).c_str());
  for (auto& x : r.reasons) std::printf("%s,", std::string(to_string(x)).c_str());
  std::printf("]\n");
  return 0;
}
