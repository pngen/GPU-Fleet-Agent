#include "gpufleetagent/core/result.hpp"

namespace gpufleet {

const char* to_string(Status s) noexcept {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Corrupt: return "corrupt";
    case Status::Truncated: return "truncated";
    case Status::OutOfBounds: return "out_of_bounds";
    case Status::InvalidEnum: return "invalid_enum";
    case Status::InvalidGeneration: return "invalid_generation";
    case Status::ProtocolMismatch: return "protocol_mismatch";
    case Status::VersionMismatch: return "version_mismatch";
    case Status::Duplicate: return "duplicate";
    case Status::UnknownMessage: return "unknown_message";
    case Status::Malformed: return "malformed";
    case Status::Stale: return "stale";
    case Status::NotAuthoritative: return "not_authoritative";
    case Status::NotFound: return "not_found";
    case Status::AlreadyExists: return "already_exists";
    case Status::Quarantined: return "quarantined";
    case Status::Drained: return "drained";
    case Status::Unsupported: return "unsupported";
    case Status::Io: return "io";
    case Status::IdentityMismatch: return "identity_mismatch";
    case Status::CapacityExceeded: return "capacity_exceeded";
    case Status::Rejected: return "rejected";
    case Status::Internal:
    default: return "internal";
  }
}

}  // namespace gpufleet
