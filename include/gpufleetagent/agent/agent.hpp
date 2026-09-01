#pragma once
// Worker agent.
//
// A real worker-agent OS process that connects to a coordinator over framed
// TCP, registers with a fresh WorkerBootId, publishes device/capability
// observations from a DeviceBackend, sends heartbeats, handles admin drain/
// quarantine commands, and reconnects on failure. On restart it generates a
// NEW WorkerBootId so that any message from the prior process incarnation is
// fenced at the coordinator.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gpufleetagent/device/device_backend.hpp"
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/fleet/registration.hpp"
#include "gpufleetagent/protocol/message.hpp"
#include "gpufleetagent/observation/observation.hpp"
#include "gpufleetagent/core/clock.hpp"
#include "gpufleetagent/transport/transport.hpp"

namespace gpufleet {

class WorkerAgent {
 public:
  struct Options {
    std::string coordinator_host = "127.0.0.1";
    std::uint16_t coordinator_port = 0;
    NodeId node{1};
    WorkerId worker{1};
    // If zero (default), a fresh WorkerBootId is generated at construction.
    WorkerBootId boot{};
    std::string os_platform = "unknown";
    DurationMs heartbeat_interval_ms = 250;
    DurationMs reconnect_backoff_ms = 100;
    std::string backend = "synthetic";  // "synthetic" | "cuda"
  };

  explicit WorkerAgent(Options opts);
  ~WorkerAgent();
  WorkerAgent(const WorkerAgent&) = delete;

  Result<void> run();      // connect + register + loop until stop()
  void stop();
  const Options& options() const { return opts_; }
  WorkerBootId boot() const { return opts_.boot; }
  DeviceBackend* backend() { return backend_.get(); }

  /// Build an observation for the given enumerated device (used by examples/
  /// harnesses and by the agent loop itself).
  static DeviceObservation to_observation(const EnumeratedDevice& dev, const DeviceProbe& probe,
                                          const ObservationMetadata& meta);

 private:
  Result<void> connect_and_register(TcpStream& stream);
  Result<void> publish_snapshot(TcpStream& stream);
  Result<void> send_heartbeat(TcpStream& stream);
  void handle_admin(TcpStream& stream, const Message& msg);
  void reconnect_loop();
  Result<void> read_one_message(TcpStream& stream, Message& msg);

  Options opts_;
  CoordinatorEpoch last_known_epoch_{};
  std::unique_ptr<DeviceBackend> backend_;
  std::atomic<bool> running_{false};
  SystemClock clock_;
  RegistrationGeneration reg_gen_{1};
  ObservationGeneration obs_gen_{1};
  HealthGeneration health_gen_{1};
  DeviceGeneration device_gen_{1};
  std::thread loop_thread_;
};

}  // namespace gpufleet
