#pragma once
// Separately typed generations. Each authority domain rolls on its own
// timeline and is never collapsed into a single generic counter.
#include "gpufleetagent/core/strong.hpp"

namespace gpufleet {

struct CoordinatorEpochTag;
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;

struct FleetGenerationTag;
using FleetGeneration = Generation<FleetGenerationTag>;

struct NodeGenerationTag;
using NodeGeneration = Generation<NodeGenerationTag>;

struct WorkerGenerationTag;
using WorkerGeneration = Generation<WorkerGenerationTag>;

struct DeviceGenerationTag;
using DeviceGeneration = Generation<DeviceGenerationTag>;

struct ObservationGenerationTag;
using ObservationGeneration = Generation<ObservationGenerationTag>;

struct CapabilityGenerationTag;
using CapabilityGeneration = Generation<CapabilityGenerationTag>;

struct HealthGenerationTag;
using HealthGeneration = Generation<HealthGenerationTag>;

struct RegistrationGenerationTag;
using RegistrationGeneration = Generation<RegistrationGenerationTag>;

struct PolicyGenerationTag;
using PolicyGeneration = Generation<PolicyGenerationTag>;

}  // namespace gpufleet
