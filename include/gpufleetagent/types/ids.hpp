#pragma once
// Concrete strong identifier types used across GPU Fleet Agent.
#include "gpufleetagent/core/strong.hpp"

namespace gpufleet {

// Fleet-scope identifiers.
struct FleetIdTag;
using FleetId = StrongId<FleetIdTag>;

// Node / worker (agent) identifiers.
struct NodeIdTag;
using NodeId = StrongId<NodeIdTag>;
struct WorkerIdTag;
using WorkerId = StrongId<WorkerIdTag>;
struct WorkerBootIdTag;
using WorkerBootId = StrongId<WorkerBootIdTag>;

// Device identifiers.
struct DeviceIdTag;
using DeviceId = StrongId<DeviceIdTag>;

// Observation and report identifiers.
struct ObservationIdTag;
using ObservationId = StrongId<ObservationIdTag>;
struct CapabilityIdTag;
using CapabilityId = StrongId<CapabilityIdTag>;
struct HealthReportIdTag;
using HealthReportId = StrongId<HealthReportIdTag>;
struct LeaseIdTag;
using LeaseId = StrongId<LeaseIdTag>;
struct RegistrationIdTag;
using RegistrationId = StrongId<RegistrationIdTag>;

// Authority identifiers.
struct CoordinatorIdTag;
using CoordinatorId = StrongId<CoordinatorIdTag>;
struct AgentIdTag;
using AgentId = StrongId<AgentIdTag>;

}  // namespace gpufleet
