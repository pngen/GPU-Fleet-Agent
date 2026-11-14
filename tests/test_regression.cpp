// Regression tests for audited defects: a rejected or superseded connection
// never fences the live incarnation, a worker re-registering with its own boot
// after a disconnect regains authority, a rejected observation batch changes
// nothing, the state file is replaced atomically, and snapshot loads reject
// invalid enums.
#include "tests/test_fw.hpp"
#include "gpufleetagent/coordinator/coordinator.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/identity/device_identity.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/persistence/store.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/transport/transport.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <windows.h>
#undef ERROR

using namespace gpufleet;

namespace {

Registration make_reg(WorkerId w, WorkerBootId b, std::uint64_t gen) {
  Registration reg;
  reg.node = NodeId(1u);
  reg.worker = w;
  reg.worker_boot = b;
  reg.protocol_version.value = 1u;
  reg.registration_generation = RegistrationGeneration(gen);
  reg.epoch = CoordinatorEpoch(1u);
  reg.agent_version.version = SemanticVersion{1, 0, 0};
  return reg;
}

DeviceObservation obs_for(WorkerId worker, WorkerBootId boot, const std::string& uuid) {
  DeviceObservation o;
  o.device_id = DeviceId(1u);
  o.observation_generation = ObservationGeneration(1u);
  o.health_generation = HealthGeneration(1u);
  o.observed_at = 5000;
  o.source_worker_boot = boot;
  o.source_worker = worker;
  o.source_node = NodeId(1u);
  o.epoch = CoordinatorEpoch(1u);
  o.device_generation = DeviceGeneration(1u);
  o.identity.vendor = AcceleratorVendor::Nvidia;
  o.identity.vendor_name = "NVIDIA";
  o.identity.uuid.value = uuid;
  o.identity.architecture = "blackwell";
  o.identity.compute_capability = ComputeCapability{12, 0};
  o.identity.total_physical_memory = 32ull * 1024 * 1024 * 1024;
  o.identity.driver_version.text = "13040";
  o.enumerated = true; o.present = true;
  o.driver_runtime_ok = true; o.cuda_init_ok = true;
  o.memory_alloc_ok = true; o.h2d_ok = true; o.kernel_exec_ok = true;
  o.sync_ok = true; o.d2h_ok = true; o.verify_ok = true; o.core_validation_ok = true;
  o.total_memory = o.identity.total_physical_memory;
  o.free_memory = 24ull * 1024 * 1024 * 1024;
  o.used_memory = 8ull * 1024 * 1024 * 1024;
  return o;
}

ObservationPolicy policy() {
  ObservationPolicy p;
  p.freshness_threshold_ms = 5000;
  return p;
}

// Sends one message and returns the coordinator's reply.
bool rpc(TcpStream& s, const Message& m, Message& reply) {
  auto body = encode_message(m);
  auto frame = encode_frame(m.type, body);
  if (!s.send_all(frame).ok()) return false;
  FrameDecoder dec;
  std::vector<std::uint8_t> buf(64 * 1024);
  Frame pending;
  std::string perr;
  for (;;) {
    auto r = s.recv_some_opt(buf);
    if (!r.ok()) return false;
    if (!r.value().has_value()) continue;
    if (*r.value() == 0) return false;
    auto res = dec.feed(std::span<const std::uint8_t>(buf.data(), *r.value()), pending, perr);
    if (res == FrameDecoder::Result::FrameReady) {
      auto dm = decode_message(pending.payload);
      if (!dm.ok()) return false;
      reply = dm.move_value();
      return true;
    }
    if (res == FrameDecoder::Result::Error) return false;
  }
}

bool register_on(TcpStream& s, WorkerId w, WorkerBootId b, std::uint64_t gen) {
  Message m;
  m.type = MessageType::REGISTER;
  m.worker = w;
  m.worker_boot = b;
  m.epoch = CoordinatorEpoch(1u);
  m.payload = encode_registration(make_reg(w, b, gen));
  Message reply;
  return rpc(s, m, reply) && reply.ok && reply.type == MessageType::REGISTER_ACK;
}

// Half-closes the connection and reads until the coordinator closes its side:
// by then the coordinator has finished handling this disconnect.
void close_and_drain(TcpStream& s) {
  ::shutdown(static_cast<SOCKET>(s.handle()), SD_SEND);
  std::vector<std::uint8_t> buf(4096);
  for (;;) {
    auto r = s.recv_some_opt(buf);
    if (!r.ok()) break;
    if (!r.value().has_value()) continue;
    if (*r.value() == 0) break;
  }
  s.close();
}

void test_rejected_and_superseded_connections_do_not_fence() {
  Coordinator::Options o;
  o.heartbeat_timeout_ms = 24LL * 3600 * 1000;  // liveness must not interfere
  Coordinator coord(o);
  CHECK(coord.start().ok());
  const std::string host = "127.0.0.1";
  const WorkerId w(10u);

  auto c1 = TcpStream::connect(host, coord.port(), 3000);
  CHECK(c1.ok());
  TcpStream s1 = c1.move_value();
  CHECK(register_on(s1, w, WorkerBootId(100u), 1));

  // A second incarnation is refused while the first is alive; its
  // connection closing must not fence the first.
  auto c2 = TcpStream::connect(host, coord.port(), 3000);
  CHECK(c2.ok());
  TcpStream s2 = c2.move_value();
  CHECK(!register_on(s2, w, WorkerBootId(200u), 1));
  close_and_drain(s2);
  StateSnapshot snap = coord.store().snapshot();
  CHECK(snap.workers.at(w).boot == WorkerBootId(100u));
  CHECK(!snap.workers.at(w).lost && !snap.workers.at(w).fenced);

  // The first incarnation is declared lost (liveness) and a new one
  // registers; the superseded connection closing must not fence the new one.
  CHECK(coord.store().mark_worker_lost(w, now_millis()).ok());
  auto c3 = TcpStream::connect(host, coord.port(), 3000);
  CHECK(c3.ok());
  TcpStream s3 = c3.move_value();
  CHECK(register_on(s3, w, WorkerBootId(300u), 1));
  close_and_drain(s1);
  snap = coord.store().snapshot();
  CHECK(snap.workers.at(w).boot == WorkerBootId(300u));
  CHECK(!snap.workers.at(w).lost && !snap.workers.at(w).fenced);

  close_and_drain(s3);
  coord.stop();
}

void test_same_boot_reregistration_regains_authority() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  const WorkerId w(10u);
  const WorkerBootId b(100u);
  CHECK(store.register_worker(make_reg(w, b, 1), 1000).ok());
  CHECK(store.mark_worker_lost(w, 1100).ok());              // its connection dropped
  CHECK(store.register_worker(make_reg(w, b, 2), 1200).ok());  // the same process reconnects
  CHECK(store.ingest_snapshot(w, {obs_for(w, b, "GPU-a")}, policy(), 5100).ok());
  StateSnapshot s = store.snapshot();
  CHECK(!s.workers.at(w).fenced && !s.workers.at(w).lost);
  CHECK(store.accounting().live_workers == 1u);
}

void test_rejected_batch_changes_nothing() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  const WorkerId w(10u);
  const WorkerBootId b(100u);
  CHECK(store.register_worker(make_reg(w, b, 1), 1000).ok());
  DeviceObservation good = obs_for(w, b, "GPU-good");
  DeviceObservation stale = obs_for(w, b, "GPU-stale");
  stale.epoch = CoordinatorEpoch(9u);
  CHECK(!store.ingest_snapshot(w, {good, stale}, policy(), 5100).ok());
  CHECK(!store.device_exists(canonical_device_identity(good.identity)));
  CHECK(store.accepted_observations() == 0u);
  CHECK(store.changes().empty());
}

void test_state_file_replaced_atomically() {
  char dir[MAX_PATH];
  GetTempPathA(MAX_PATH, dir);
  const std::string path = std::string(dir) + "gfa_atomic_" + std::to_string(GetCurrentProcessId()) + ".gfle";
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  CHECK(store.register_worker(make_reg(WorkerId(10u), WorkerBootId(100u), 1), 1000).ok());
  const StateSnapshot first = store.snapshot();
  CHECK(save_snapshot(path, first).ok());
  CHECK(store.register_worker(make_reg(WorkerId(11u), WorkerBootId(110u), 1), 1100).ok());
  const StateSnapshot second = store.snapshot();

  // Another process holds the temp file without delete sharing, so this save
  // cannot complete its replacement: the previous state must survive intact.
  const std::string tmp = path + ".tmp";
  HANDLE hold = CreateFileA(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(hold != INVALID_HANDLE_VALUE);
  const bool blocked_save_ok = save_snapshot(path, second).ok();
  CloseHandle(hold);
  CHECK(!blocked_save_ok);
  auto kept = load_snapshot(path);
  CHECK(kept.ok());
  if (kept.ok()) CHECK(snapshot_digest(kept.value()) == snapshot_digest(first));

  // Once nothing holds the temp file, the save replaces the state.
  CHECK(save_snapshot(path, second).ok());
  auto replaced = load_snapshot(path);
  CHECK(replaced.ok());
  if (replaced.ok()) CHECK(snapshot_digest(replaced.value()) == snapshot_digest(second));
  std::remove(path.c_str());
  std::remove(tmp.c_str());
}

void test_void_results_report_errors() {
  // An error returned through Result<void> must not read as success.
  Result<void> err = error_result(Status::Io, "failed");
  CHECK(!err.ok());
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(5u));
  CHECK(!store.roll_epoch(CoordinatorEpoch(3u)).ok());      // backwards: refused
  CHECK(store.epoch() == CoordinatorEpoch(5u));

  // A coordinator whose state file cannot be recovered must not start fresh.
  char dir[MAX_PATH];
  GetTempPathA(MAX_PATH, dir);
  const std::string state = std::string(dir) + "gfa_corrupt_" + std::to_string(GetCurrentProcessId()) + ".gfle";
  { FILE* f = std::fopen(state.c_str(), "wb"); std::fputs("not a GFLE store", f); std::fclose(f); }
  Coordinator::Options o;
  o.state_file = state;
  Coordinator coord(o);
  CHECK(!coord.start().ok());
  coord.stop();
  std::remove(state.c_str());
}

void test_snapshot_rejects_invalid_enums() {
  FleetStateStore store(CoordinatorId(1u), CoordinatorEpoch(1u));
  const WorkerId w(10u);
  const WorkerBootId b(100u);
  CHECK(store.register_worker(make_reg(w, b, 1), 1000).ok());
  CHECK(store.ingest_snapshot(w, {obs_for(w, b, "GPU-e")}, policy(), 5100).ok());
  const StateSnapshot base = store.snapshot();
  CHECK(!base.devices.empty());
  CHECK(decode_snapshot(encode_snapshot(base)).ok());
  auto decoded = [&](auto mutate) {
    StateSnapshot s = base;
    mutate(s);
    return decode_snapshot(encode_snapshot(s)).ok();
  };
  CHECK(!decoded([](StateSnapshot& s) { s.devices.begin()->second.health = static_cast<HealthState>(200); }));
  CHECK(!decoded([](StateSnapshot& s) { s.devices.begin()->second.eligibility = static_cast<EligibilityState>(200); }));
  CHECK(!decoded([](StateSnapshot& s) { s.devices.begin()->second.drain = static_cast<DrainState>(9); }));
  CHECK(!decoded([](StateSnapshot& s) {
    s.devices.begin()->second.eligibility_reasons = {static_cast<EligibilityReason>(99)};
  }));
  CHECK(!decoded([](StateSnapshot& s) {
    Capability c;
    c.kind = static_cast<CapabilityKind>(99);
    s.devices.begin()->second.capabilities.push_back(c);
  }));
  CHECK(!decoded([](StateSnapshot& s) {
    ChangeRecord c;
    c.kind = static_cast<ChangeKind>(0);
    s.change_log.push_back(c);
  }));
}

}  // namespace

int main() {
  test_rejected_and_superseded_connections_do_not_fence();
  test_same_boot_reregistration_regains_authority();
  test_rejected_batch_changes_nothing();
  test_state_file_replaced_atomically();
  test_void_results_report_errors();
  test_snapshot_rejects_invalid_enums();
  return tf::summary("regression");
}
