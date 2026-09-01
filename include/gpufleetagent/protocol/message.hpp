#pragma once
// Framed binary control-protocol message model.
//
// All wire messages carry a typed authority envelope (coordinator, worker,
// node, agent) plus the generations that fence each axis. The envelope is
// checked by the coordinator/agent state machine for staleness; the codec
// validates structural integrity (known types, valid enums, bounded lengths,
// no trailing garbage).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gpufleetagent/types/ids.hpp"
#include "gpufleetagent/types/generations.hpp"

namespace gpufleet {

enum class MessageType : std::uint8_t {
  HELLO = 1,
  REGISTER = 2,
  REGISTER_ACK = 3,
  HEARTBEAT = 4,
  DEVICE_SNAPSHOT = 5,
  HEALTH_REPORT = 6,
  CAPABILITY_REPORT = 7,
  DRAIN = 8,
  UNDRAIN = 9,
  QUARANTINE = 10,
  CLEAR_QUARANTINE = 11,
  SNAPSHOT_REQUEST = 12,
  SNAPSHOT_RESPONSE = 13,
  ACK = 14,
  ERROR = 15,
  // Internal control extensions (the required set is not exhaustive).
  EPOCH_ROLL = 16,
  SAVE = 17,
};

constexpr std::string_view to_string(MessageType t) {
  switch (t) {
    case MessageType::HELLO: return "HELLO";
    case MessageType::REGISTER: return "REGISTER";
    case MessageType::REGISTER_ACK: return "REGISTER_ACK";
    case MessageType::HEARTBEAT: return "HEARTBEAT";
    case MessageType::DEVICE_SNAPSHOT: return "DEVICE_SNAPSHOT";
    case MessageType::HEALTH_REPORT: return "HEALTH_REPORT";
    case MessageType::CAPABILITY_REPORT: return "CAPABILITY_REPORT";
    case MessageType::DRAIN: return "DRAIN";
    case MessageType::UNDRAIN: return "UNDRAIN";
    case MessageType::QUARANTINE: return "QUARANTINE";
    case MessageType::CLEAR_QUARANTINE: return "CLEAR_QUARANTINE";
    case MessageType::SNAPSHOT_REQUEST: return "SNAPSHOT_REQUEST";
    case MessageType::SNAPSHOT_RESPONSE: return "SNAPSHOT_RESPONSE";
    case MessageType::ACK: return "ACK";
    case MessageType::ERROR: return "ERROR";
    case MessageType::EPOCH_ROLL: return "EPOCH_ROLL";
    case MessageType::SAVE: return "SAVE";
    default: return "UNKNOWN";
  }
}

inline bool valid_message_type(std::uint8_t v) {
  return v >= static_cast<std::uint8_t>(MessageType::HELLO) &&
         v <= static_cast<std::uint8_t>(MessageType::SAVE);
}

/// A single control message. The envelope carries the identity and authority
/// generations for every axis so the peer can fence stale traffic; the payload
/// carries the message-specific structured data.
struct Message {
  MessageType type = MessageType::HELLO;

  CoordinatorId coordinator{};
  CoordinatorEpoch epoch{};
  WorkerBootId worker_boot{};
  WorkerId worker{};
  NodeId node{};
  AgentId agent{};
  RegistrationGeneration reg_gen{};
  ObservationGeneration obs_gen{};
  HealthGeneration health_gen{};
  DeviceGeneration device_gen{};
  CapabilityGeneration cap_gen{};
  std::uint64_t request_id = 0;
  bool ok = true;                 // for ACK/ERROR
  std::string reason;             // for ERROR
  std::vector<std::uint8_t> payload;  // message-specific serialized body
};

}  // namespace gpufleet
