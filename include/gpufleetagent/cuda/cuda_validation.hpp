#pragma once
// Real CUDA device validation.
//
// This performs more than cudaGetDeviceCount(): it enumerates, initializes,
// allocates a bounded buffer (this is a SYSTEM runtime, so the allocation is
// bounded on every call), copies a known input H2D, executes a real kernel,
// synchronizes, copies D2H, verifies against a CPU reference, frees, and
// re-queries memory to confirm return to baseline (or a justified bounded
// delta). The proof result is published as VALIDATED execution capability.
//
// Declarations here are CUDA-agnostic so any translation unit can include
// them; the implementation (which contains __global__ code) is compiled by
// nvcc.
#include <cstddef>
#include <cstdint>
#include <string>

namespace gpufleet {

struct CudaValidationResult {
  bool ok = false;
  std::size_t elements = 0;
  std::uint64_t baseline_free = 0;
  std::uint64_t baseline_used = 0;
  std::uint64_t after_free = 0;
  std::uint64_t after_used = 0;
  std::int64_t delta_free = 0;
  bool memory_baseline_restored = false;
  bool alloc_ok = false;
  bool h2d_ok = false;
  bool kernel_ok = false;
  bool sync_ok = false;
  bool d2h_ok = false;
  bool verify_ok = false;
  std::string detail;
};

/// Run the full validation sequence on device p ordinal. The result is
/// deterministic in the sense that it either proves the sequence or records
/// exactly which step failed. Bounded allocation: at most a few MiB.
CudaValidationResult run_cuda_validation(int ordinal);

}  // namespace gpufleet
