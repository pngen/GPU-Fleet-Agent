#include "tests/test_fw.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"

using namespace gpufleet;

int main() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  Registration reg;
  reg.node = NodeId(1u); reg.worker = WorkerId(10u); reg.worker_boot = WorkerBootId(100u);
  reg.protocol_version.value = 1u;
  reg.registration_generation = RegistrationGeneration(1u);
  reg.epoch = CoordinatorEpoch(1u);
  reg.agent_version.version = SemanticVersion{1,0,0};

  // Register OK.
  auto r = store.register_worker(reg, 1000);
  CHECK(r.ok());

  // Same boot re-register with a stale registration generation -> rejected.
  Registration reg2 = reg;
  reg2.registration_generation = RegistrationGeneration(0u);
  auto r2 = store.register_worker(reg2, 1100);
  CHECK(!r2.ok());
  CHECK(r2.error().code == Status::Stale);

  // Same boot re-register with a newer generation -> OK.
  Registration reg3 = reg;
  reg3.registration_generation = RegistrationGeneration(2u);
  CHECK(store.register_worker(reg3, 1200).ok());

  // Different boot while the existing registration is alive -> duplicate rejected.
  Registration reg4 = reg;
  reg4.worker_boot = WorkerBootId(200u);
  auto r4 = store.register_worker(reg4, 1300);
  CHECK(!r4.ok());
  CHECK(r4.error().code == Status::Duplicate);

  // Mark worker lost, then a different boot can supersede.
  CHECK(store.mark_worker_lost(WorkerId(10u), 1400).ok());
  Registration reg5 = reg;
  reg5.worker_boot = WorkerBootId(300u);
  reg5.registration_generation = RegistrationGeneration(3u);
  // After mark_worker_lost, the prior is fenced/lost, so a new boot is accepted
  // and supersedes the old incarnation (WorkerBootId restart fencing).
  CHECK(store.register_worker(reg5, 1500).ok());

  // WorkerBootId restart semantics: the new boot is authoritative; memory of old boot fenced.
  auto snap = store.snapshot();
  CHECK(snap.workers.at(WorkerId(10u)).boot == WorkerBootId(300u));

  return tf::summary("registration");
}
