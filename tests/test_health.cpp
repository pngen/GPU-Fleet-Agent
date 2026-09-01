#include "tests/test_fw.hpp"
#include "gpufleetagent/health/health_state.hpp"
#include "gpufleetagent/health/health_evaluator.hpp"

using namespace gpufleet;

int main() {
  // All-healthy.
  HealthSignals ok;
  ok.enumerated = true; ok.device_present = true; ok.was_known = true;
  ok.driver_runtime_ok = true; ok.cuda_init_ok = true;
  ok.memory_alloc_ok = true; ok.kernel_exec_ok = true; ok.sync_ok = true;
  ok.mem_roundtrip_ok = true; ok.last_validation_ok = true;
  auto h = evaluate_health(ok);
  CHECK(h.state == HealthState::HEALTHY);

  // Not enumerated -> OFFLINE (never known).
  HealthSignals never; never.device_present = false; never.was_known = false;
  CHECK(evaluate_health(never).state == HealthState::OFFLINE);

  // Was known but now absent -> LOST.
  HealthSignals lost; lost.device_present = false; lost.was_known = true;
  CHECK(evaluate_health(lost).state == HealthState::LOST);

  // Fatal error -> UNHEALTHY.
  HealthSignals fatal = ok; fatal.fatal_error = true;
  CHECK(evaluate_health(fatal).state == HealthState::UNHEALTHY);

  // Driver missing -> UNHEALTHY.
  HealthSignals nodrv = ok; nodrv.driver_runtime_ok = false;
  CHECK(evaluate_health(nodrv).state == HealthState::UNHEALTHY);

  // Repeated failures -> UNHEALTHY.
  HealthSignals rep = ok; rep.consecutive_failures = 5; rep.failure_threshold = 3;
  CHECK(evaluate_health(rep).state == HealthState::UNHEALTHY);

  // Stale observation -> RECOVERING (never treated as HEALTHY).
  HealthSignals stale = ok; stale.observation_stale = true;
  CHECK(evaluate_health(stale).state == HealthState::RECOVERING);

  // Quarantine dominates.
  HealthSignals quar = ok; quar.quarantined = true;
  CHECK(evaluate_health(quar).state == HealthState::QUARANTINED);

  // Drain dominates.
  HealthSignals drain = ok; drain.explicitly_drained = true; drain.inflight_work_remaining = true;
  CHECK(evaluate_health(drain).state == HealthState::DRAINING);
  HealthSignals drained = ok; drained.explicitly_drained = true;
  CHECK(evaluate_health(drained).state == HealthState::DRAINED);

  // Degraded from temp/power.
  HealthSignals deg = ok; deg.temperature_within_limits = false;
  CHECK(evaluate_health(deg).state == HealthState::DEGRADED);

  // Core not proven, last validation OK -> RECOVERING.
  HealthSignals rec = ok; rec.memory_alloc_ok = false; rec.last_validation_ok = true;
  CHECK(evaluate_health(rec).state == HealthState::RECOVERING);

  // Core not proven, never validated -> UNHEALTHY.
  HealthSignals bad = ok; bad.kernel_exec_ok = false; bad.last_validation_ok = false;
  CHECK(evaluate_health(bad).state == HealthState::UNHEALTHY);

  return tf::summary("health");
}
