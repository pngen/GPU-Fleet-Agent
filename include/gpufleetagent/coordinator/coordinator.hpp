#pragma once
// Distributed coordinator.
//
// The coordinator is a real process that owns the canonical fleet state, enforces
// the authority/fencing protocol over framed TCP, and is the single authority
// for "what does this accelerator worker actually have and what can it safely
// do now." It listens on a host/port, accepts worker-agent and admin/control
// connections, validates every message's authority envelope, applies
// observations through the pipeline, and persists/recover state.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gpufleetagent/fleet/fleet_state.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/transport/transport.hpp"
#include "gpufleetagent/persistence/store.hpp"
#include "gpufleetagent/core/clock.hpp"

namespace gpufleet {

class Coordinator {
 public:
  struct Options {
    std::string bind_host = "127.0.0.1";
    std::uint16_t port = 0;                 // 0 => ephemeral
    std::string state_file;                 // empty => no persistence
    CoordinatorId coordinator_id{1};
    CoordinatorEpoch initial_epoch{1};
    DurationMs heartbeat_timeout_ms = 5000;
  };

  explicit Coordinator(Options opts);
  ~Coordinator();
  Coordinator(const Coordinator&) = delete;
  Coordinator& operator=(const Coordinator&) = delete;

  Result<void> start();      // bind the listener (and recover persistence if configured)
  void stop();

  std::uint16_t port() const { return opts_.port; }
  FleetStateStore& store() { return store_; }
  const Options& options() const { return opts_; }
  Result<void> save_state() const;
  Result<void> load_state();  // recover persistence (called by start if configured)

 private:
  void accept_loop();
  void handle_connection(TcpStream stream);
  void liveness_loop();
  void dispatch(TcpStream& stream, const Message& msg);
  void send_message(TcpStream& stream, const Message& msg);
  void respond_ack(TcpStream& stream, const Message& req, bool ok, const std::string& reason);

  Options opts_;
  TcpListener listener_;
  FleetStateStore store_;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  std::thread liveness_thread_;
  std::vector<std::thread> conn_threads_;
  mutable std::mutex conn_mu_;
  SystemClock clock_;
};

}  // namespace gpufleet
