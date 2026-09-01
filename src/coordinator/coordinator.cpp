#include "gpufleetagent/coordinator/coordinator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "gpufleetagent/observation/observation_codec.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/admin_payload.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/core/codec.hpp"

namespace gpufleet {


Coordinator::Coordinator(Options opts)
    : opts_(std::move(opts)), store_(opts_.coordinator_id, opts_.initial_epoch) {}

Coordinator::~Coordinator() { stop(); }

Result<void> Coordinator::start() {
  auto bound = TcpListener::bind(opts_.bind_host, opts_.port);
  if (!bound.ok()) return error_result(Status::Io, "coordinator bind failed: " + bound.error().message);
  listener_ = bound.move_value();
  opts_.port = listener_.port();

  if (!opts_.state_file.empty()) {
    auto r = load_state();
    if (!r.ok()) return r;
  }

  running_.store(true);
  accept_thread_ = std::thread([this] { accept_loop(); });
  liveness_thread_ = std::thread([this] { liveness_loop(); });
  return ok_result();
}

Result<void> Coordinator::load_state() {
  if (opts_.state_file.empty()) return ok_result();
  // Only recover if the file exists.
  FILE* f = std::fopen(opts_.state_file.c_str(), "rb");
  if (!f) return ok_result();
  std::fclose(f);
  auto snap = load_snapshot(opts_.state_file);
  if (!snap.ok()) return error_result(Status::Corrupt, "cannot recover state: " + snap.error().message);
  return store_.restore(snap.value());
}

Result<void> Coordinator::save_state() const {
  if (opts_.state_file.empty()) return error_result(Status::Unsupported, "no state_file configured");
  StateSnapshot snap = store_.snapshot();
  return save_snapshot(opts_.state_file, snap);
}

void Coordinator::stop() {
  if (!running_.exchange(false)) return;
  listener_.close();
  if (accept_thread_.joinable()) accept_thread_.join();
  if (liveness_thread_.joinable()) liveness_thread_.join();
  {
    std::lock_guard<std::mutex> lk(conn_mu_);
    for (auto& t : conn_threads_) if (t.joinable()) t.join();
    conn_threads_.clear();
  }
}

void Coordinator::accept_loop() {
  while (running_.load()) {
    auto c = listener_.accept(100);
    if (!c.ok()) {
      if (running_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    std::lock_guard<std::mutex> lk(conn_mu_);
    conn_threads_.emplace_back([this, s = c.move_value()]() mutable { handle_connection(std::move(s)); });
  }
}

void Coordinator::handle_connection(TcpStream stream) {
  FrameDecoder dec;
  std::vector<std::uint8_t> buf(64 * 1024);
  Frame pending;
  std::string perr;
  WorkerId connected_worker{};
  bool worker_bound = false;

  while (running_.load()) {
    // recv_some_opt distinguishes "no data right now" (keep the connection
    // alive) from a clean close (0) and from a real reset. Closing on a mere
    // recv timeout would truncate the HELLO->REGISTER handshake, so a timeout
    // must never be treated as a disconnect.
    auto recv = stream.recv_some_opt(buf);
    if (!recv.ok()) break;   // real reset/error -> close
    if (!recv.value().has_value()) continue;  // no data yet -> keep waiting
    std::size_t n = *recv.value();
    if (n == 0) break;       // clean close

    auto res = dec.feed(std::span<const std::uint8_t>(buf.data(), n), pending, perr);
    while (res == FrameDecoder::Result::FrameReady) {
      auto dm = decode_message(pending.payload);
      if (!dm.ok()) {
        // Malformed payload; respond ERROR and drop the connection.
        Message err; err.type = MessageType::ERROR; err.reason = dm.error().message;
        send_message(stream, err);
        return;
      }
      Message msg = dm.move_value();
      dispatch(stream, msg);
      if (msg.type == MessageType::REGISTER) {
        connected_worker = msg.worker;
        worker_bound = true;
      }
      res = dec.feed(std::span<const std::uint8_t>(), pending, perr);
    }
    if (res == FrameDecoder::Result::Error) {
      Message err; err.type = MessageType::ERROR; err.reason = perr;
      send_message(stream, err);
      return;
    }
  }

  // Connection closed. If a worker was registered on this connection and is
  // still authoritative, mark it lost.
  if (worker_bound && running_.load()) {
    std::int64_t now = clock_.now();
    store_.mark_worker_lost(connected_worker, now);
  }
}

void Coordinator::liveness_loop() {
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    Timestamp now = clock_.now();
    auto snap = store_.snapshot();
    for (const auto& [wid, wr] : snap.workers) {
      if (wr.alive && !wr.lost && !wr.fenced && wr.last_heartbeat != kInvalidTimestamp) {
        if (now - wr.last_heartbeat > opts_.heartbeat_timeout_ms) {
          store_.mark_worker_lost(wid, now);
        }
      }
    }
  }
}

void Coordinator::dispatch(TcpStream& stream, const Message& msg) {
  Timestamp now = clock_.now();

  switch (msg.type) {
    case MessageType::HELLO: {
      Message ack;
      ack.type = MessageType::ACK;
      ack.epoch = store_.epoch();
      ack.coordinator = opts_.coordinator_id;
      ack.request_id = msg.request_id;
      ack.ok = true;
      send_message(stream, ack);
      return;
    }
    case MessageType::REGISTER: {
      auto reg = decode_registration(msg.payload);
      if (!reg.ok()) {
        respond_ack(stream, msg, false, "malformed registration: " + reg.error().message);
        return;
      }
      auto r = store_.register_worker(reg.value(), now);
      if (!r.ok()) {
        respond_ack(stream, msg, false, "registration rejected: " + r.error().message);
        return;
      }
      Message ack;
      ack.type = MessageType::REGISTER_ACK;
      ack.request_id = msg.request_id;
      ack.worker_boot = reg.value().worker_boot;
      ack.worker = reg.value().worker;
      ack.epoch = store_.epoch();
      ack.ok = true;
      ByteWriter w; w.u64(reg.value().registration_generation.value()); w.u64(store_.epoch().value());
      ack.payload = w.take();
      send_message(stream, ack);
      return;
    }
    case MessageType::HEARTBEAT: {
      if (!msg.worker_boot.is_zero() || !msg.worker.is_zero()) {
        // normal worker heartbeat
        auto r = store_.heartbeat(msg.worker, msg.worker_boot, msg.epoch, now, msg.health_gen);
        if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
        respond_ack(stream, msg, true, "");
        return;
      }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::DEVICE_SNAPSHOT:
    case MessageType::HEALTH_REPORT:
    case MessageType::CAPABILITY_REPORT: {
      auto obs = decode_observation_batch(msg.payload);
      if (!obs.ok()) { respond_ack(stream, msg, false, "malformed observation batch"); return; }
      ObservationPolicy policy;
      auto r = store_.ingest_snapshot(msg.worker, obs.value(), policy, now);
      if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::SNAPSHOT_REQUEST: {
      StateSnapshot snap = store_.snapshot();
      Message resp;
      resp.type = MessageType::SNAPSHOT_RESPONSE;
      resp.request_id = msg.request_id;
      resp.epoch = store_.epoch();
      resp.payload = encode_snapshot(snap);
      resp.ok = true;
      send_message(stream, resp);
      return;
    }
    case MessageType::DRAIN:
    case MessageType::UNDRAIN: {
      auto t = decode_drain_target(msg.payload);
      if (!t.ok()) { respond_ack(stream, msg, false, "malformed drain target"); return; }
      std::string identity = t.value().identity;
      DrainState ds = t.value().level;
      auto snap = store_.snapshot();
      WorkerId owner{};
      for (const auto& [wid, wr] : snap.workers) {
        for (auto& id : wr.device_identities) if (id == identity) { owner = wid; break; }
        if (!owner.is_zero()) break;
      }
      auto r = store_.set_drain(owner, identity, ds, now);
      if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::QUARANTINE: {
      auto q = decode_quarantine(msg.payload);
      if (!q.ok()) { respond_ack(stream, msg, false, "malformed quarantine"); return; }
      auto snap = store_.snapshot();
      WorkerId owner{};
      for (const auto& [wid, wr] : snap.workers) {
        for (auto& id : wr.device_identities) if (id == q.value().identity) { owner = wid; break; }
        if (!owner.is_zero()) break;
      }
      QuarantineRecord rec;
      rec.reason = q.value().reason;
      rec.source = q.value().source;
      rec.generation = q.value().generation;
      rec.at = now;
      rec.authority = store_.epoch();
      DeviceId did{};
      auto r = store_.set_quarantine(owner, q.value().identity, rec, now);
      if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::CLEAR_QUARANTINE: {
      auto identity = decode_identity_target(msg.payload);
      if (!identity.ok()) { respond_ack(stream, msg, false, "malformed clear target"); return; }
      auto r = store_.clear_quarantine(WorkerId{}, identity.value(), store_.epoch(), now);
      if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::EPOCH_ROLL: {
      auto r = store_.roll_epoch(msg.epoch);
      if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::SAVE: {
      auto r = save_state();
      if (!r.ok()) { respond_ack(stream, msg, false, r.error().message); return; }
      respond_ack(stream, msg, true, "");
      return;
    }
    case MessageType::ACK:
    case MessageType::ERROR:
    case MessageType::REGISTER_ACK:
    case MessageType::SNAPSHOT_RESPONSE:
    default:
      respond_ack(stream, msg, false, "unexpected message type");
      return;
  }
}

void Coordinator::respond_ack(TcpStream& stream, const Message& req, bool ok, const std::string& reason) {
  Message ack;
  ack.type = ok ? MessageType::ACK : MessageType::ERROR;
  ack.ok = ok;
  ack.reason = reason;
  ack.request_id = req.request_id;
  ack.epoch = store_.epoch();
  ack.worker_boot = req.worker_boot;
  ack.worker = req.worker;
  send_message(stream, ack);
}

void Coordinator::send_message(TcpStream& stream, const Message& msg) {
  auto body = encode_message(msg);
  auto frame = encode_frame(msg.type, body);
  stream.send_all(frame);
}

}  // namespace gpufleet
