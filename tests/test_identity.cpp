#include "tests/test_fw.hpp"
#include "gpufleetagent/identity/device_identity.hpp"
#include "gpufleetagent/observation/change.hpp"
#include "gpufleetagent/observation/observation.hpp"

using namespace gpufleet;

int main() {
  DeviceIdentity d;
  d.vendor = AcceleratorVendor::Nvidia;
  d.vendor_name = "NVIDIA";
  d.uuid.value = "GPU-abc";
  d.pci = PciAddress{0, 1, 0, 0};
  d.architecture = "blackwell";
  d.compute_capability = ComputeCapability{12, 0};
  d.total_physical_memory = 32ull * 1024 * 1024 * 1024;
  d.driver_version.text = "13040";

  CHECK(d.is_stable());
  CHECK_EQ(d.compute_capability.to_string(), "12.0");
  CHECK_EQ(d.compute_capability.sm_string(), "sm_120");
  CHECK_EQ(d.pci.to_string(), "0000:01:00.0");

  // PCI alone is not stable identity.
  DeviceIdentity pci_only;
  pci_only.pci = PciAddress{0, 1, 0, 0};
  CHECK(!pci_only.is_stable());

  // compute capability comparison
  CHECK((ComputeCapability{12,0} >= ComputeCapability{9,0}));
  CHECK((ComputeCapability{9,0} < ComputeCapability{12,0}));
  CHECK((ComputeCapability{12,0} == ComputeCapability{12,0}));

  // canonical identity is deterministic and distinguishes changes.
  DeviceIdentity d2 = d; d2.uuid.value = "GPU-xyz";
  CHECK(canonical_device_identity(d) != canonical_device_identity(d2));
  CHECK_EQ(canonical_device_identity(d), canonical_device_identity(d));

  // change detection: identity changed vs absent
  DeviceState prior, cand;
  prior.present = true;
  cand.present = true;
  prior.identity = d; cand.identity = d;
  auto ch = diff_device_state(prior, cand, 100, DeviceGeneration(1), CoordinatorEpoch(1));
  CHECK_EQ(ch.size(), 0u);

  cand.identity.uuid.value = "GPU-new";
  cand.health = HealthState::HEALTHY; prior.health = HealthState::UNKNOWN;
  cand.eligibility = EligibilityState::ELIGIBLE; prior.eligibility = EligibilityState::UNKNOWN;
  auto ch2 = diff_device_state(prior, cand, 100, DeviceGeneration(2), CoordinatorEpoch(1));
  bool saw_identity=false, saw_health=false;
  for (auto& c : ch2) {
    if (c.kind == ChangeKind::IDENTITY_CHANGED) saw_identity = true;
    if (c.kind == ChangeKind::HEALTH_CHANGED) saw_health = true;
  }
  CHECK(saw_identity);
  CHECK(saw_health);

  return tf::summary("identity");
}
