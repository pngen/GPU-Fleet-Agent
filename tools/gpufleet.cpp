// GPU Fleet Agent CLI.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gpufleetagent/version.hpp"
#include "gpufleetagent/core/clock.hpp"
#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"
#include "gpufleetagent/identity/device_identity.hpp"
#include "gpufleetagent/health/health_state.hpp"
#include "gpufleetagent/eligibility/eligibility.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/fleet/snapshot_codec.hpp"
#include "gpufleetagent/persistence/store.hpp"
#include "gpufleetagent/coordinator/coordinator.hpp"
#include "gpufleetagent/agent/agent.hpp"
#include "gpufleetagent/protocol/framing.hpp"
#include "gpufleetagent/protocol/codec.hpp"
#include "gpufleetagent/protocol/admin_payload.hpp"
#include "gpufleetagent/transport/transport.hpp"
#include "gpufleetagent/cuda/cuda_backend.hpp"

#include <cstring>

namespace gpufleet {
namespace cli {
namespace {

struct Params {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
  std::string state;
  std::string device;
  std::string worker;
  std::string reason;
  std::string source = "admin";
  std::string backend = "synthetic";
  std::string node = "1";
  std::string worker_id = "1";
  std::string boot;
  bool json = false;
  std::string command;
};

std::string jesc(const std::string& s) {
  std::string o; o.reserve(s.size());
  for (char c : s) {
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c == '\t') o += "\\t";
    else o += c;
  }
  return o;
}
std::string J(const std::string& s) { return "\"" + jesc(s) + "\""; }

Result<Message> request(const Params& p, Message req) {
  auto c = TcpStream::connect(p.host, p.port, 5000);
  if (!c.ok()) return error_result(Status::Io, "connect: " + c.error().message);
  TcpStream s = c.move_value();
  s.set_timeout(5000);
  auto body = encode_message(req);
  auto frame = encode_frame(req.type, body);
  auto sr = s.send_all(frame);
  if (!sr.ok()) return error_result(Status::Io, "send: " + sr.error().message);
  FrameDecoder dec; std::vector<std::uint8_t> buf(64 * 1024); Frame pending; std::string perr;
  for (int i = 0; i < 64; ++i) {
    auto rr = s.recv_some_opt(buf);
    if (!rr.ok()) return error_result(Status::Io, "recv: " + rr.error().message);
    if (!rr.value().has_value()) continue;
    if (*rr.value() == 0) return error_result(Status::Io, "connection closed before reply");
    auto res = dec.feed(std::span<const std::uint8_t>(buf.data(), *rr.value()), pending, perr);
    while (res == FrameDecoder::Result::FrameReady) {
      auto dm = decode_message(pending.payload);
      if (dm.ok()) return ok_result(dm.move_value());
      res = dec.feed(std::span<const std::uint8_t>(), pending, perr);
    }
    if (res == FrameDecoder::Result::Error) return error_result(Status::Io, "frame error: " + perr);
  }
  return error_result(Status::Io, "no reply from coordinator");
}

StateSnapshot fetch_snapshot(const Params& p) {
  Message req; req.type = MessageType::SNAPSHOT_REQUEST;
  auto r = request(p, req);
  if (!r.ok()) { std::cerr << "error: " << r.error().message << std::endl; std::exit(2); }
  if (r.value().type != MessageType::SNAPSHOT_RESPONSE) { std::cerr << "error: unexpected reply" << std::endl; std::exit(2); }
  auto s = decode_snapshot(r.value().payload);
  if (!s.ok()) { std::cerr << "error: snapshot decode failed: " << s.error().message << std::endl; std::exit(2); }
  return s.move_value();
}

void print_node(const NodeRecord& n, bool json) {
  if (json) std::cout << "{\"node\":" << n.node.to_string() << ",\"gen\":" << n.generation.to_string() << "}";
  else std::cout << "node " << n.node.to_string() << " gen " << n.generation.to_string();
}
void print_worker(const WorkerRecord& w, bool json) {
  if (json) {
    std::cout << "{\"worker\":" << w.worker.to_string() << ",\"boot\":" << w.boot.to_string()
              << ",\"node\":" << w.node.to_string() << ",\"alive\":" << (w.alive ? "true" : "false")
              << ",\"fenced\":" << (w.fenced ? "true" : "false")
              << ",\"lost\":" << (w.lost ? "true" : "false")
              << ",\"os\":" << J(w.os_platform) << ",\"devices\":" << w.device_identities.size() << "}";
  } else {
    std::cout << "worker " << w.worker.to_string() << " boot " << w.boot.to_string()
              << " node " << w.node.to_string();
  }
}
void print_device(const DeviceState& d, bool json) {
  const std::string key = canonical_device_identity(d.identity);
  if (json) {
    std::cout << "{\"id\":" << d.device_id.to_string() << ",\"key\":" << J(key)
              << ",\"vendor\":" << J(std::string(to_string(d.identity.vendor)))
              << ",\"pci\":" << J(d.identity.pci.to_string())
              << ",\"arch\":" << J(d.identity.architecture)
              << ",\"health\":" << J(std::string(to_string(d.health)))
              << ",\"eligibility\":" << J(std::string(to_string(d.eligibility)))
              << ",\"present\":" << (d.present ? "true" : "false")
              << ",\"drain\":" << J(std::string(to_string(d.drain)))
              << ",\"quarantined\":" << (d.quarantined ? "true" : "false")
              << ",\"fresh\":" << (d.observation_fresh ? "true" : "false")
              << ",\"totalMem\":" << d.total_memory << ",\"usedMem\":" << d.used_memory
              << ",\"freeMem\":" << d.free_memory << "}";
  } else {
    std::cout << "device " << d.device_id.to_string() << " key=" << key
              << " " << to_string(d.identity.vendor) << " pci=" << d.identity.pci.to_string()
              << " arch=" << d.identity.architecture << " health=" << to_string(d.health)
              << " elig=" << to_string(d.eligibility) << " fresh=" << (d.observation_fresh ? "yes" : "no");
  }
}

int cmd_run_coordinator(const Params& p) {
  Coordinator::Options opts; opts.bind_host = p.host; opts.port = p.port; opts.state_file = p.state;
  Coordinator coord(opts);
  auto r = coord.start(); if (!r.ok()) { std::cerr << "coordinator error: " << r.error().message << std::endl; return 2; }
  std::cout << "coordinator 127.0.0.1:" << coord.port() << " epoch=" << coord.store().epoch().to_string() << std::endl;
  std::cout.flush();
  while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
int cmd_run_agent(const Params& p) {
  WorkerAgent::Options opts; opts.coordinator_host = p.host; opts.coordinator_port = p.port;
  opts.node = NodeId(std::stoull(p.node)); opts.worker = WorkerId(std::stoull(p.worker_id));
  if (!p.boot.empty()) opts.boot = WorkerBootId(std::stoull(p.boot));
  opts.backend = p.backend;
  WorkerAgent agent(opts);
  std::cout << "agent worker=" << opts.worker.to_string() << " boot=" << agent.boot().to_string() << " backend=" << opts.backend << std::endl;
  std::cout.flush();
  return agent.run().ok() ? 0 : 2;
}
int cmd_snapshot(const Params& p) {
  StateSnapshot s = fetch_snapshot(p);
  if (p.json) std::cout << "{\"epoch\":" << s.epoch.to_string() << ",\"workers\":" << s.workers.size()
                        << ",\"devices\":" << s.devices.size() << "}";
  else {
    std::cout << "epoch=" << s.epoch.to_string() << " coordinator=" << s.coordinator.to_string() << std::endl;
    for (auto& [wk, wr] : s.workers) { print_worker(wr, false); std::cout << std::endl; }
    for (auto& [key, d] : s.devices) { print_device(d, false); std::cout << std::endl; }
  }
  std::cout << std::endl;
  return 0;
}
int cmd_list(const Params& p, const char* kind) {
  StateSnapshot s = fetch_snapshot(p); bool first = true;
  if (p.json) std::cout << "[";
  if (std::strcmp(kind, "nodes") == 0) { for (auto& [nk, n] : s.nodes) { if (!first) std::cout << ","; first=false; print_node(n, p.json); } }
  else if (std::strcmp(kind, "workers") == 0) { for (auto& [wk, w] : s.workers) { if (!first) std::cout << ","; first=false; print_worker(w, p.json); } }
  else { for (auto& [key, d] : s.devices) { if (!first) std::cout << ","; first=false; print_device(d, p.json); } }
  if (p.json) std::cout << "]";
  std::cout << std::endl; return 0;
}
int cmd_health(const Params& p) { StateSnapshot s = fetch_snapshot(p);
  for (auto& [key, d] : s.devices) std::cout << to_string(d.health) << " " << to_string(d.eligibility) << " " << key << std::endl; return 0; }
int cmd_eligibility(const Params& p) { StateSnapshot s = fetch_snapshot(p);
  for (auto& [key, d] : s.devices) { std::cout << to_string(d.eligibility) << " " << key << " [";
    for (std::size_t i = 0; i < d.eligibility_reasons.size(); ++i) { if (i) std::cout << ","; std::cout << to_string(d.eligibility_reasons[i]); }
    std::cout << "]" << std::endl; } return 0; }
int cmd_capabilities(const Params& p) { StateSnapshot s = fetch_snapshot(p);
  for (auto& [key, d] : s.devices) { std::cout << "device " << key << ":" << std::endl;
    for (auto& c : d.capabilities) std::cout << "  " << c.name << "=" << c.value << " [" << to_string(c.kind) << "]" << std::endl; }
  return 0; }
int cmd_changes(const Params& p) { StateSnapshot s = fetch_snapshot(p);
  for (auto& c : s.change_log) std::cout << to_string(c.kind) << " device=" << c.device_id.to_string()
    << " at=" << c.at << " gen=" << c.generation.to_string() << " epoch=" << c.epoch.to_string() << std::endl; return 0; }
int cmd_inspect_device(const Params& p) { StateSnapshot s = fetch_snapshot(p);
  for (auto& [key, d] : s.devices) { if (!p.device.empty() && key.find(p.device) == std::string::npos) continue; print_device(d, p.json); std::cout << std::endl; } return 0; }
int cmd_inspect_worker(const Params& p) { StateSnapshot s = fetch_snapshot(p);
  for (auto& [wk, w] : s.workers) { if (!p.worker.empty() && wk.to_string() != p.worker) continue; print_worker(w, p.json); std::cout << std::endl; } return 0; }
int cmd_admin(const Params& p, const char* which) {
  Message m;
  if (std::strcmp(which, "save") == 0) m.type = MessageType::SAVE;
  else if (std::strcmp(which, "drain") == 0) { m.type = MessageType::DRAIN; m.payload = encode_drain_target(p.device, DrainState::DRAINING); }
  else if (std::strcmp(which, "undrain") == 0) { m.type = MessageType::UNDRAIN; m.payload = encode_drain_target(p.device, DrainState::ACTIVE); }
  else if (std::strcmp(which, "quarantine") == 0) { m.type = MessageType::QUARANTINE; m.payload = encode_quarantine(p.device, p.reason, p.source, DeviceGeneration(1)); }
  else if (std::strcmp(which, "clear-quarantine") == 0) { m.type = MessageType::CLEAR_QUARANTINE; m.payload = encode_identity_target(p.device); }
  auto r = request(p, m); if (!r.ok()) { std::cerr << "error: " << r.error().message << std::endl; return 2; }
  std::cout << (r.value().ok ? "ok" : ("rejected: " + r.value().reason)) << std::endl;
  return r.value().ok ? 0 : 1;
}
int cmd_recover(const Params& p) {
  auto snap = load_snapshot(p.state);
  if (!snap.ok()) { std::cerr << "recover failed: " << snap.error().message << std::endl; return 2; }
  std::cout << "recovered epoch=" << snap.value().epoch.to_string() << " workers=" << snap.value().workers.size()
            << " devices=" << snap.value().devices.size() << " digest=" << snapshot_digest(snap.value()) << std::endl; return 0;
}
int cmd_save_local(const Params& p) {
  StateSnapshot s = fetch_snapshot(p);
  auto r = save_snapshot(p.state, s); if (!r.ok()) { std::cerr << "save failed: " << r.error().message << std::endl; return 2; }
  std::cout << "saved " << p.state << " digest=" << snapshot_digest(s) << std::endl; return 0;
}
int cmd_info() {
  std::cout << "GPU Fleet Agent " << version_string() << " by " << kPublisher << std::endl;
  std::cout << "protocol=" << protocol_version() << " store_schema=" << store_schema_version() << std::endl;
  std::cout << "cuda: " << cuda_driver_summary() << std::endl; return 0;
}
void usage() { std::cout << "GPU Fleet Agent CLI\n" << "commands: run-coordinator run-agent list-devices list-workers list-nodes \n"
  << "inspect-device inspect-worker health eligibility capabilities changes snapshot save \n"
  << "drain undrain quarantine clear-quarantine recover info\n"; }
Params parse(int argc, char** argv) {
  Params p;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--host") p.host = next();
    else if (a == "--port") p.port = static_cast<std::uint16_t>(std::stoi(next()));
    else if (a == "--state") p.state = next();
    else if (a == "--device") p.device = next();
    else if (a == "--worker") p.worker = next();
    else if (a == "--reason") p.reason = next();
    else if (a == "--source") p.source = next();
    else if (a == "--backend") p.backend = next();
    else if (a == "--node-id") p.node = next();
    else if (a == "--worker-id") p.worker_id = next();
    else if (a == "--boot-id") p.boot = next();
    else if (a == "--json") p.json = true;
    else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
    else if (p.command.empty()) p.command = a;
  }
  return p;
}
}  // namespace
}  // namespace cli
}  // namespace gpufleet

int main(int argc, char** argv) {
  gpufleet::cli::Params p = gpufleet::cli::parse(argc, argv);
  using namespace gpufleet;
  if (p.command == "run-coordinator") return cli::cmd_run_coordinator(p);
  if (p.command == "run-agent") return cli::cmd_run_agent(p);
  if (p.command == "snapshot") return cli::cmd_snapshot(p);
  if (p.command == "list-devices") return cli::cmd_list(p, "devices");
  if (p.command == "list-workers") return cli::cmd_list(p, "workers");
  if (p.command == "list-nodes") return cli::cmd_list(p, "nodes");
  if (p.command == "health") return cli::cmd_health(p);
  if (p.command == "eligibility") return cli::cmd_eligibility(p);
  if (p.command == "capabilities") return cli::cmd_capabilities(p);
  if (p.command == "changes") return cli::cmd_changes(p);
  if (p.command == "inspect-device") return cli::cmd_inspect_device(p);
  if (p.command == "inspect-worker") return cli::cmd_inspect_worker(p);
  if (p.command == "drain") return cli::cmd_admin(p, "drain");
  if (p.command == "undrain") return cli::cmd_admin(p, "undrain");
  if (p.command == "quarantine") return cli::cmd_admin(p, "quarantine");
  if (p.command == "clear-quarantine") return cli::cmd_admin(p, "clear-quarantine");
  if (p.command == "save") return cli::cmd_save_local(p);
  if (p.command == "recover") return cli::cmd_recover(p);
  if (p.command == "info") return cli::cmd_info();
  cli::usage();
  return 1;
}
