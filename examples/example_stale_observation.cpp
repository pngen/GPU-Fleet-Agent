#include <cstdio>
#include "gpufleetagent/observation/pipeline.hpp"
#include "gpufleetagent/device/device_backend.hpp"
static gpufleet::DeviceProbe healthy_probe() {
  gpufleet::DeviceProbe p;
  p.enumerated = true; p.present = true; p.driver_runtime_ok = true; p.cuda_init_ok = true;
  p.memory_alloc_ok = true; p.h2d_ok = true; p.kernel_exec_ok = true; p.sync_ok = true;
  p.d2h_ok = true; p.verify_ok = true; p.core_validation_ok = true;
  p.identity.vendor = gpufleet::AcceleratorVendor::Nvidia; p.identity.architecture = "blackwell";
  p.identity.compute_capability = gpufleet::ComputeCapability{12,0};
  p.identity.total_physical_memory = 32ull*1024*1024*1024; p.identity.driver_version.text = "13040";
  return p;
}
int main() {
  std::printf("=== stale observation ===\n");
  gpufleet::ObservationPolicy policy; policy.freshness_threshold_ms = 1000;
  gpufleet::ObservationMetadata meta;
  meta.device_id = gpufleet::DeviceId(1); meta.observation_generation = gpufleet::ObservationGeneration(1);
  meta.health_generation = gpufleet::HealthGeneration(1);
  meta.observed_at = 1000; meta.source_worker_boot = gpufleet::WorkerBootId(1);
  meta.source_worker = gpufleet::WorkerId(1); meta.source_node = gpufleet::NodeId(1);
  meta.epoch = gpufleet::CoordinatorEpoch(1); meta.device_generation = gpufleet::DeviceGeneration(1);
  auto o = gpufleet::normalize_observation(healthy_probe(), meta);
  auto a = gpufleet::apply_observation(gpufleet::DeviceState{}, o, policy, 1500, gpufleet::CoordinatorEpoch(1));
  std::printf("fresh (now=1500) => %s\n", std::string(to_string(a.state.eligibility)).c_str());
  // Re-apply with a now far beyond the threshold.
  auto b = gpufleet::apply_observation(gpufleet::DeviceState{}, o, policy, 100000, gpufleet::CoordinatorEpoch(1));
  std::printf("stale (now=100000) => %s\n", std::string(to_string(b.state.eligibility)).c_str());
  return 0;
}
