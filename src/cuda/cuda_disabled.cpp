// Provides a graceful no-CUDA fallback for the CUDA backend entry points.
// When GPU Fleet Agent is built without the NVIDIA CUDA toolkit, the CUDA
// backend is a clean no-op: cuda_available() returns false and make_cuda_backend
// returns nullptr. This keeps the runtime usable on hosts without CUDA.
#include "gpufleetagent/cuda/cuda_backend.hpp"

#if !GPUFLEETAGENT_HAVE_CUDA
#include <string>

namespace gpufleet {

bool cuda_available() { return false; }

std::string cuda_driver_summary() { return "CUDA backend not built (GPUFLEETAGENT_HAVE_CUDA=0)"; }

std::unique_ptr<DeviceBackend> make_cuda_backend() { return nullptr; }

}  // namespace gpufleet
#endif
