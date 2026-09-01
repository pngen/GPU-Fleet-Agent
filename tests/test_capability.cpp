#include "tests/test_fw.hpp"
#include "gpufleetagent/capability/capability.hpp"

using namespace gpufleet;

int main() {
  Capability c;
  c.id = CapabilityId(1u);
  c.name = std::string(capname::tensor_cores);
  c.kind = CapabilityKind::INFERRED;
  c.value = "true";

  CHECK_EQ(c.name, "tensor_cores");
  CHECK(c.kind == CapabilityKind::INFERRED);
  CHECK_EQ(std::string(to_string(c.kind)), "inferred");
  CHECK(c != Capability{});

  // Distinct kinds serialize distinct text.
  CHECK_EQ(std::string(to_string(CapabilityKind::DISCOVERED)), "discovered");
  CHECK_EQ(std::string(to_string(CapabilityKind::VALIDATED)), "validated");
  CHECK_EQ(std::string(to_string(CapabilityKind::UNKNOWN)), "unknown");

  return tf::summary("capability");
}
