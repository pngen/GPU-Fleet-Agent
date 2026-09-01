cmake_minimum_required(VERSION 3.24)

project(GPUFleetAgent
        VERSION 1.0.0
        DESCRIPTION "GPU Fleet Agent - authoritative accelerator fleet state and execution eligibility runtime"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GPUFLEETAGENT_BUILD_TESTS "Build GPU Fleet Agent tests" ON)
option(GPUFLEETAGENT_BUILD_EXAMPLES "Build GPU Fleet Agent examples" ON)
option(GPUFLEETAGENT_BUILD_BENCHMARKS "Build GPU Fleet Agent benchmarks" ON)
option(GPUFLEETAGENT_BUILD_CLI "Build the gpufleet CLI" ON)
option(GPUFLEETAGENT_WITH_CUDA "Enable the NVIDIA/CUDA probing backend" ON)
option(GPUFLEETAGENT_ENABLE_CUDA_VALIDATION "Enable real RTX 5090 CUDA validation proof" ON)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Warnings: zero-warning policy. MSVC uses /W4 /WX; GCC/Clang use -Wall -Wextra -Werror.
# ---------------------------------------------------------------------------
if(MSVC)
  # /W4 /WX are applied to CXX only. nvcc on Windows misparses MSVC-style
  # slashed flags (e.g. /W4) as file paths, so CUDA sources must not receive
  # them directly; nvcc forwards host flags through -Xcompiler.
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/permissive->)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# CUDA support (optional; backend degrades gracefully when absent).
# ---------------------------------------------------------------------------
set(GPUFLEETAGENT_HAVE_CUDA FALSE)
if(GPUFLEETAGENT_WITH_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    # Target sm_120 (compute capability 12.0 / RTX 5090) unless the user set
    # CMAKE_CUDA_ARCHITECTURES explicitly. Set BEFORE enable_language so CMake
    # does not auto-detect a legacy architecture.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL "")
      set(CMAKE_CUDA_ARCHITECTURES 120)
    endif()
    enable_language(CUDA)
    set(GPUFLEETAGENT_HAVE_CUDA TRUE)
    find_package(CUDAToolkit REQUIRED)
  else()
    message(STATUS "GPU Fleet Agent: CUDA compiler not found; building without the CUDA backend.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(GPUFLEETAGENT_SOURCES
  src/core/result.cpp
  src/transport/transport.cpp
  src/coordinator/coordinator.cpp
  src/agent/agent.cpp
  src/device/synthetic_backend.cpp
  src/cuda/cuda_disabled.cpp
)

if(GPUFLEETAGENT_HAVE_CUDA)
  list(APPEND GPUFLEETAGENT_SOURCES
    src/cuda/cuda_backend.cpp
    src/cuda/cuda_validation.cpp
  )
  set_source_files_properties(src/cuda/cuda_backend.cpp src/cuda/cuda_validation.cpp PROPERTIES LANGUAGE CUDA)
endif()

add_library(gpufleet STATIC ${GPUFLEETAGENT_SOURCES})
add_library(gpufleet::gpufleet ALIAS gpufleet)

set_target_properties(gpufleet PROPERTIES
  OUTPUT_NAME gpufleet
  EXPORT_NAME gpufleet
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(gpufleet PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_definitions(gpufleet PUBLIC
  GPUFLEETAGENT_VERSION_STRING="${PROJECT_VERSION}"
  GPUFLEETAGENT_HAVE_CUDA=$<BOOL:${GPUFLEETAGENT_HAVE_CUDA}>)

if(WIN32)
  target_link_libraries(gpufleet PRIVATE ws2_32)
endif()

if(GPUFLEETAGENT_HAVE_CUDA)
  target_link_libraries(gpufleet PUBLIC CUDA::cudart CUDA::cuda_driver)
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if(GPUFLEETAGENT_BUILD_CLI)
  add_executable(gpufleet_cli tools/gpufleet.cpp)
  target_link_libraries(gpufleet_cli PRIVATE gpufleet)
  set_target_properties(gpufleet_cli PROPERTIES OUTPUT_NAME gpufleet)
endif()

# ---------------------------------------------------------------------------
# Tests / examples / benchmarks
# ---------------------------------------------------------------------------
if(GPUFLEETAGENT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(GPUFLEETAGENT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(GPUFLEETAGENT_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Installation / packaging (find_package(GPUFleetAgent CONFIG REQUIRED))
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS gpufleet
        EXPORT GPUFleetAgentTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT GPUFleetAgentTargets
        FILE GPUFleetAgentTargets.cmake
        NAMESPACE GPUFleetAgent::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPUFleetAgent)

configure_package_config_file(
  cmake/GPUFleetAgentConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/GPUFleetAgentConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPUFleetAgent)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/GPUFleetAgentConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/GPUFleetAgentConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/GPUFleetAgentConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GPUFleetAgent)

if(GPUFLEETAGENT_BUILD_CLI)
  install(TARGETS gpufleet_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
