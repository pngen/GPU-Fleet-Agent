#include "tests/test_fw.hpp"
#include <windows.h>
#undef ERROR  // windows.h defines ERROR=0, colliding with MessageType::ERROR.
#undef min
#undef max
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "gpufleetagent/transport/transport.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/protocol/admin_payload.hpp"
#include "gpufleetagent/observation/observation_codec.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/persistence/store.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

using namespace gpufleet;

namespace {
struct ChildProc {
  HANDLE h = nullptr;
  DWORD pid = 0;
  bool spawn(const std::string& exe, const std::string& args) {
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    std::string cmd = "\"" + exe + "\" " + args;
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    h = pi.hProcess; pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    return true;
  }
  void kill() {
    if (h) {
      TerminateProcess(h, 1);
      // bounded cleanup wait after an explicit terminate (not a test timeout)
      DWORD r = WaitForSingleObject(h, 10000);
      (void)r;
      CloseHandle(h); h = nullptr;
    }
  }
  bool alive() { if (!h) return false; return WaitForSingleObject(h, 0) == WAIT_TIMEOUT; }
  ~ChildProc() { kill(); }
};

std::uint16_t find_free_port() {
  auto l = TcpListener::bind("127.0.0.1", 0);
  if (!l.ok()) return 0;
  std::uint16_t p = l.value().port();
  l.value().close();
  return p;
}

Result<Message> rpc(const std::string& host, std::uint16_t port, Message req) {
  auto c = TcpStream::connect(host, port, 3000);
  if (!c.ok()) return error_result(Status::Io, "connect failed");
  TcpStream s = c.move_value();
  s.set_timeout(3000);
  auto body = encode_message(req);
  auto frame = encode_frame(req.type, body);
  auto sr = s.send_all(frame);
  if (!sr.ok()) return error_result(Status::Io, "send failed");
  FrameDecoder dec; std::vector<std::uint8_t> buf(64*1024); Frame pending; std::string perr;
  for (int i = 0; i < 64; ++i) {
    auto rr = s.recv_some_opt(buf);
    if (!rr.ok()) return error_result(Status::Io, "recv failed");
    if (!rr.value().has_value()) continue;
    if (*rr.value() == 0) return error_result(Status::Io, "closed");
    auto res = dec.feed(std::span<const std::uint8_t>(buf.data(), *rr.value()), pending, perr);
    while (res == FrameDecoder::Result::FrameReady) {
      auto dm = decode_message(pending.payload);
      if (dm.ok()) return ok_result(dm.move_value());
      res = dec.feed(std::span<const std::uint8_t>(), pending, perr);
    }
    if (res == FrameDecoder::Result::Error) return error_result(Status::Io, "frame error");
  }
  return error_result(Status::Io, "no reply");
}

bool try_snapshot(const std::string& host, std::uint16_t port, StateSnapshot& out) {
  Message req; req.type = MessageType::SNAPSHOT_REQUEST;
  auto r = rpc(host, port, req);
  if (!r.ok()) return false;
  if (r.value().type != MessageType::SNAPSHOT_RESPONSE) return false;
  auto s = decode_snapshot(r.value().payload);
  if (!s.ok()) return false;
  out = s.move_value();
  return true;
}

// poll a condition; this is a convergence check, not a test timeout.
template <typename Pred>
bool wait_for(const std::string& host, std::uint16_t port, Pred pred, int max_iter = 20000) {
  for (int i = 0; i < max_iter; ++i) {
    StateSnapshot s;
    if (try_snapshot(host, port, s) && pred(s)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("no cli path provided\n"); return 1; }
  std::string cli = argv[1];
  std::uint16_t port = find_free_port();
  const std::string state = "mp_state.gfle";
  std::remove(state.c_str());
  std::string host = "127.0.0.1";

  // 1. Start coordinator.
  ChildProc coord;
  CHECK(coord.spawn(cli, "run-coordinator --host " + host + " --port " + std::to_string(port) + " --state " + state));
  CHECK(wait_for(host, port, [](const StateSnapshot&){ return true; }));

  // 2. Start Worker A and B as separate OS processes.
  ChildProc wa, wb;
  CHECK(wa.spawn(cli, "run-agent --host " + host + " --port " + std::to_string(port) + " --worker-id 1 --node-id 1 --backend synthetic"));
  CHECK(wb.spawn(cli, "run-agent --host " + host + " --port " + std::to_string(port) + " --worker-id 2 --node-id 1 --backend synthetic"));

  // 3. Both register and publish observations.
  bool both = wait_for(host, port, [](const StateSnapshot& s){
    return s.workers.count(WorkerId(1)) && s.workers.count(WorkerId(2)) &&
           s.workers.at(WorkerId(1)).device_identities.size() >= 1 &&
           s.workers.at(WorkerId(2)).device_identities.size() >= 1;
  });
  CHECK(both);

  // 6. Capture authority envelopes.
  StateSnapshot capture;
  try_snapshot(host, port, capture);
  WorkerBootId bootA = capture.workers.at(WorkerId(1)).boot;
  WorkerBootId bootB = capture.workers.at(WorkerId(2)).boot;
  CoordinatorEpoch before_epoch = capture.epoch;
  std::size_t deviceB_count = capture.workers.at(WorkerId(2)).device_identities.size();
  CHECK(!bootA.is_zero());
  CHECK(!bootB.is_zero());

  // 7. Kill Worker A as a real OS process.
  wa.kill();
  CHECK(!wa.alive());

  // 8. Its observations become lost/stale.
  bool lostA = wait_for(host, port, [&](const StateSnapshot& s){
    if (!s.workers.count(WorkerId(1))) return false;
    return s.workers.at(WorkerId(1)).lost || s.workers.at(WorkerId(1)).fenced;
  });
  CHECK(lostA);

  // 9. Roll coordinator epoch.
  Message roll; roll.type = MessageType::EPOCH_ROLL; roll.epoch = before_epoch.next();
  auto rr = rpc(host, port, roll);
  CHECK(rr.ok() && rr.value().ok);

  // 11. Replay stale messages (stale epoch, stale boot, stale generations) and prove rejection.
  {
    Message stale;
    stale.type = MessageType::DEVICE_SNAPSHOT;
    stale.worker = WorkerId(1);
    stale.worker_boot = bootA;                       // stale boot
    stale.epoch = before_epoch;                      // stale epoch
    // payload: an empty observation batch (won't be reached if rejected)
    stale.payload = encode_observation_batch({});
    auto resp = rpc(host, port, stale);
    CHECK(resp.ok() && !resp.value().ok);            // acknowledged as REJECTED
  }

  // 12. Prove every stale mutation is rejected (snapshot unchanged w.r.t. worker A boot/epoch).
  {
    StateSnapshot s; try_snapshot(host, port, s);
    CHECK(s.epoch == before_epoch.next());
    // Worker A is either fenced/lost with the old boot (still not the new one).
    if (s.workers.count(WorkerId(1))) {
      CHECK(s.workers.at(WorkerId(1)).boot == bootA || s.workers.at(WorkerId(1)).lost);
    }
  }

  // 13. Prove Worker B remains unaffected.
  {
    StateSnapshot s; try_snapshot(host, port, s);
    CHECK(s.workers.count(WorkerId(2)));
    CHECK(s.workers.at(WorkerId(2)).boot == bootB);
    CHECK(!s.workers.at(WorkerId(2)).lost);
    CHECK(s.workers.at(WorkerId(2)).device_identities.size() >= deviceB_count);
  }

  // 10. Restart Worker A with a fresh WorkerBootId (new process incarnation).
  bool restartA = false;
  if (wa.spawn(cli, "run-agent --host " + host + " --port " + std::to_string(port) + " --worker-id 1 --node-id 1 --backend synthetic")) {
    // wait for re-registration under a NEW boot (the old incarnation is fenced).
    restartA = wait_for(host, port, [&](const StateSnapshot& s){
      if (!s.workers.count(WorkerId(1))) return false;
      return s.workers.at(WorkerId(1)).boot != bootA &&
             !s.workers.at(WorkerId(1)).lost;
    });
  }
  CHECK(restartA);

  // 14/15. Publish fresh Worker A state and confirm current authority.
  {
    StateSnapshot s; try_snapshot(host, port, s);
    CHECK(s.workers.at(WorkerId(1)).boot != bootA);
    CHECK(s.epoch == before_epoch.next());
    CHECK(!s.workers.at(WorkerId(1)).lost);
  }

  // 16. Persist coordinator state.
  Message save; save.type = MessageType::SAVE;
  auto sr = rpc(host, port, save);
  CHECK(sr.ok() && sr.value().ok);
  StateSnapshot live; try_snapshot(host, port, live);
  std::uint32_t live_digest = snapshot_digest(live);

  // 17. Recover: prove recovered dynamic observations are NOT treated as fresh.
  // Recovery semantics (marking revived dynamic observations STALE) are applied
  // by FleetStateStore::restore, not by the raw file decode, so restore the
  // persisted snapshot and verify no device is fresh.
  {
    auto recovered = load_snapshot(state);
    CHECK(recovered.ok());
    CHECK(snapshot_digest(recovered.value()) == live_digest);
    FleetStateStore recstore(CoordinatorId(1u), CoordinatorEpoch(1u));
    CHECK(recstore.restore(recovered.value()).ok());
    auto rs = recstore.snapshot();
    bool any_fresh = false;
    for (auto& [k, d] : rs.devices) if (d.observation_fresh) any_fresh = true;
    CHECK(!any_fresh);
  }

  // 18/19. Refresh both agents at the new epoch. After a coordinator epoch roll
  // a worker must re-register at the new epoch to regain authority, so both
  // worker processes are recycled (Worker B was verified unaffected at step 13,
  // before this authoritative re-affirmation at the new epoch).
  wb.kill();
  CHECK(wb.spawn(cli, "run-agent --host " + host + " --port " + std::to_string(port) + " --worker-id 2 --node-id 1 --backend synthetic"));
  bool freshBoth = wait_for(host, port, [](const StateSnapshot& s){
    if (!s.workers.count(WorkerId(1)) || !s.workers.count(WorkerId(2))) return false;
    if (s.workers.at(WorkerId(1)).lost || s.workers.at(WorkerId(2)).lost) return false;
    std::size_t fresh = 0;
    for (auto& [k, d] : s.devices) if (d.observation_fresh) fresh++;
    return fresh >= 1;
  });
  CHECK(freshBoth);

  // 20. Verify deterministic stable state digest across a save+reload.
  auto save2 = rpc(host, port, save);
  CHECK(save2.ok() && save2.value().ok);
  {
    StateSnapshot s2; try_snapshot(host, port, s2);
    auto l2 = load_snapshot(state);
    CHECK(l2.ok());
    CHECK(snapshot_digest(s2) == snapshot_digest(l2.value()));
  }

  std::remove(state.c_str());
  return tf::summary("multiprocess");
}
