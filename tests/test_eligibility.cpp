#include "tests/test_fw.hpp"
#include "gpufleetagent/eligibility/eligibility.hpp"
#include "gpufleetagent/eligibility/eligibility_evaluator.hpp"

using namespace gpufleet;

int main() {
  // Fully eligible.
  EligibilityInput in;
  in.health = HealthState::HEALTHY;
  in.observation_fresh = true;
  in.cuda_available = true;
  in.device_present = true;
  auto r = evaluate_eligibility(in);
  CHECK(r.state == EligibilityState::ELIGIBLE);
  CHECK_EQ(r.reasons.size(), 0u);

  // Stale observation, even HEALTHY, is never ELIGIBLE.
  EligibilityInput st = in; st.observation_fresh = false; st.health = HealthState::HEALTHY;
  auto rs = evaluate_eligibility(st);
  CHECK(rs.state == EligibilityState::STALE);
  CHECK(rs.has_reason(EligibilityReason::STALE_OBSERVATION));

  // Worker restart fences.
  EligibilityInput wr = in; wr.worker_restarted = true;
  CHECK(evaluate_eligibility(wr).state == EligibilityState::STALE);
  CHECK(evaluate_eligibility(wr).has_reason(EligibilityReason::WORKER_RESTART));

  // Generation mismatch.
  EligibilityInput gm = in; gm.observed_generation = DeviceGeneration(2); gm.current_generation = DeviceGeneration(3);
  CHECK(evaluate_eligibility(gm).state == EligibilityState::STALE);
  CHECK(evaluate_eligibility(gm).has_reason(EligibilityReason::GENERATION_MISMATCH));

  // Quarantine dominates everything.
  EligibilityInput q = in; q.quarantine = true; q.health = HealthState::HEALTHY; q.observation_fresh = true;
  CHECK(evaluate_eligibility(q).state == EligibilityState::QUARANTINED);

  // Drain dominates.
  EligibilityInput d = in; d.drain_active = true;
  CHECK(evaluate_eligibility(d).state == EligibilityState::DRAINING);
  CHECK(evaluate_eligibility(d).has_reason(EligibilityReason::DRAIN_POLICY));

  // Architecture mismatch.
  EligibilityInput am = in; am.architecture_match = false;
  auto ram = evaluate_eligibility(am);
  CHECK(ram.state == EligibilityState::INELIGIBLE);
  CHECK(ram.has_reason(EligibilityReason::ARCHITECTURE_MISMATCH));

  // Insufficient memory.
  EligibilityInput mm = in; mm.available_memory = 16; mm.required_memory = 32;
  CHECK(evaluate_eligibility(mm).state == EligibilityState::INELIGIBLE);
  CHECK(evaluate_eligibility(mm).has_reason(EligibilityReason::INSUFFICIENT_MEMORY));

  // CUDA unavailable.
  EligibilityInput cu = in; cu.cuda_available = false;
  auto rcu = evaluate_eligibility(cu);
  CHECK(rcu.state == EligibilityState::INELIGIBLE);
  CHECK(rcu.has_reason(EligibilityReason::CUDA_UNAVAILABLE));

  // Device disappeared.
  EligibilityInput dp = in; dp.device_present = false;
  CHECK(evaluate_eligibility(dp).state == EligibilityState::INELIGIBLE);
  CHECK(evaluate_eligibility(dp).has_reason(EligibilityReason::DEVICE_DISAPPEARANCE));

  // Capability mismatch.
  EligibilityInput cm = in; cm.capability_match = false;
  CHECK(evaluate_eligibility(cm).state == EligibilityState::INELIGIBLE);

  // Degraded health -> DEGRADED_ELIGIBLE.
  EligibilityInput deg = in; deg.health = HealthState::DEGRADED;
  CHECK(evaluate_eligibility(deg).state == EligibilityState::DEGRADED_ELIGIBLE);

  // UNKNOWN health -> INELIGIBLE.
  EligibilityInput unk = in; unk.health = HealthState::UNKNOWN; unk.cuda_available = true; unk.observation_fresh = true;
  CHECK(evaluate_eligibility(unk).state == EligibilityState::INELIGIBLE);

  return tf::summary("eligibility");
}
