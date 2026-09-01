#include <cstdio>
#include "gpufleetagent/cuda/cuda_validation.hpp"
#include "gpufleetagent/cuda/cuda_backend.hpp"
int main() {
  std::printf("=== hardware validation (RTX 5090, sm_120) ===\n");
  if (!gpufleet::cuda_available()) { std::printf("no CUDA device present\n"); return 0; }
  gpufleet::CudaValidationResult r = gpufleet::run_cuda_validation(0);
  std::printf("ok=%d detail=%s\n", r.ok ? 1 : 0, r.detail.c_str());
  return 0;
}
