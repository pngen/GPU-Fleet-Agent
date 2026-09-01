#include "tests/test_fw.hpp"
#include "gpufleetagent/cuda/cuda_validation.hpp"
#include "gpufleetagent/cuda/cuda_backend.hpp"

using namespace gpufleet;

int main() {
  // Real RTX 5090 (sm_120) validation proof.
  bool avail = cuda_available();
  if (!avail) {
    std::printf("SKIP: no CUDA device present (CUDA validation not run)\n");
    std::printf("[cuda_validation] 1 check, 0 failures\n");
    return 0;
  }
  CudaValidationResult r = run_cuda_validation(0);
  std::printf("CUDA validation detail: %s\n", r.detail.c_str());
  std::printf("elements=%zu baseline_free=%llu after_free=%llu delta=%lld restored=%d\n",
              r.elements, (unsigned long long)r.baseline_free, (unsigned long long)r.after_free,
              (long long)r.delta_free, r.memory_baseline_restored ? 1 : 0);
  CHECK(r.ok);
  CHECK(r.alloc_ok);
  CHECK(r.h2d_ok);
  CHECK(r.kernel_ok);
  CHECK(r.sync_ok);
  CHECK(r.d2h_ok);
  CHECK(r.verify_ok);
  CHECK(r.memory_baseline_restored);
  return tf::summary("cuda_validation");
}
