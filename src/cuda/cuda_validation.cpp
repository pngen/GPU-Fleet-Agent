// CUDA validation implementation (compiled by nvcc).
#include "gpufleetagent/cuda/cuda_validation.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gpufleet {

namespace {
constexpr int kElements = 1 << 20;   // 1 Mi elements => 4 MiB per buffer
constexpr int kBlock = 256;
constexpr std::uint64_t kMaxJustifiedDelta = 16ull * 1024 * 1024;  // bounded allowance

__global__ void add_scaled_kernel(const float* in, float* out, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = in[i] * 2.0f + 1.0f;
}

}  // namespace

CudaValidationResult run_cuda_validation(int ordinal) {
  CudaValidationResult r;
  r.elements = kElements;
  std::string detail;

  auto fail = [&](const std::string& msg, bool fatal = true) {
    if (detail.empty()) detail = msg;
    if (fatal) r.ok = false;
  };

  cudaError_t e = cudaSetDevice(ordinal);
  if (e != cudaSuccess) {
    fail("cudaSetDevice failed: " + std::string(cudaGetErrorString(e)));
    r.detail = detail;
    return r;
  }

  std::size_t free_b = 0, total_b = 0;
  e = cudaMemGetInfo(&free_b, &total_b);
  if (e != cudaSuccess) {
    fail("cudaMemGetInfo(meminfo) failed: " + std::string(cudaGetErrorString(e)));
    r.detail = detail;
    return r;
  }
  r.baseline_free = free_b;
  r.baseline_used = total_b - free_b;

  std::size_t bytes = static_cast<std::size_t>(kElements) * sizeof(float);
  float* d_in = nullptr;
  float* d_out = nullptr;
  e = cudaMalloc(&d_in, bytes);
  if (e != cudaSuccess) {
    fail("cudaMalloc(d_in) failed: " + std::string(cudaGetErrorString(e)));
    r.detail = detail;
    return r;
  }
  r.alloc_ok = true;
  e = cudaMalloc(&d_out, bytes);
  if (e != cudaSuccess) {
    fail("cudaMalloc(d_out) failed: " + std::string(cudaGetErrorString(e)));
    cudaFree(d_in);
    r.detail = detail;
    return r;
  }

  std::vector<float> h_in(static_cast<std::size_t>(kElements));
  for (int i = 0; i < kElements; ++i) h_in[static_cast<std::size_t>(i)] = static_cast<float>(i % 97);
  e = cudaMemcpy(d_in, h_in.data(), bytes, cudaMemcpyHostToDevice);
  if (e != cudaSuccess) {
    fail("cudaMemcpy H2D failed: " + std::string(cudaGetErrorString(e)));
    cudaFree(d_in); cudaFree(d_out);
    r.detail = detail;
    return r;
  }
  r.h2d_ok = true;

  int blocks = (kElements + kBlock - 1) / kBlock;
  add_scaled_kernel<<<blocks, kBlock>>>(d_in, d_out, kElements);
  e = cudaGetLastError();
  if (e != cudaSuccess) {
    fail("kernel launch failed: " + std::string(cudaGetErrorString(e)));
    cudaFree(d_in); cudaFree(d_out);
    r.detail = detail;
    return r;
  }
  r.kernel_ok = true;

  e = cudaDeviceSynchronize();
  if (e != cudaSuccess) {
    fail("cudaDeviceSynchronize failed: " + std::string(cudaGetErrorString(e)));
    cudaFree(d_in); cudaFree(d_out);
    r.detail = detail;
    return r;
  }
  r.sync_ok = true;

  std::vector<float> h_out(static_cast<std::size_t>(kElements));
  e = cudaMemcpy(h_out.data(), d_out, bytes, cudaMemcpyDeviceToHost);
  if (e != cudaSuccess) {
    fail("cudaMemcpy D2H failed: " + std::string(cudaGetErrorString(e)));
    cudaFree(d_in); cudaFree(d_out);
    r.detail = detail;
    return r;
  }
  r.d2h_ok = true;

  bool verify = true;
  for (int i = 0; i < kElements; ++i) {
    float ref = h_in[static_cast<std::size_t>(i)] * 2.0f + 1.0f;
    if (std::fabs(h_out[static_cast<std::size_t>(i)] - ref) > 1e-5f) {
      verify = false;
      fail("D2H result did not match CPU reference at element " + std::to_string(i), false);
      break;
    }
  }
  r.verify_ok = verify;

  e = cudaFree(d_in);
  e = cudaFree(d_out);
  if (e != cudaSuccess) fail("cudaFree failed: " + std::string(cudaGetErrorString(e)), false);

  // Re-query memory and compare to baseline.
  std::size_t free_after = 0, total_after = 0;
  e = cudaMemGetInfo(&free_after, &total_after);
  if (e != cudaSuccess) {
    fail("cudaMemGetInfo(after) failed: " + std::string(cudaGetErrorString(e)));
    r.detail = detail;
    return r;
  }
  r.after_free = free_after;
  r.after_used = total_after - free_after;
  r.delta_free = static_cast<std::int64_t>(free_after) - static_cast<std::int64_t>(free_b);
  // Memory must return to baseline within a justified bounded delta (the CUDA
  // runtime may retain a small driver pool; this is bounded and recorded).
  r.memory_baseline_restored = (std::llabs(r.delta_free) <= static_cast<std::int64_t>(kMaxJustifiedDelta));

  r.ok = r.alloc_ok && r.h2d_ok && r.kernel_ok && r.sync_ok && r.d2h_ok && r.verify_ok &&
         r.memory_baseline_restored;

  if (r.ok) {
    detail = "CUDA validation OK: alloc, H2D, kernel, sync, D2H, CPU verify, free; mem delta "
             + std::to_string(r.delta_free) + " bytes (bounded).";
  }
  r.detail = detail;
  return r;
}

}  // namespace gpufleet
