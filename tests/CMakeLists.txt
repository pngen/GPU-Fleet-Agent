# GPU Fleet Agent deterministic, property, concurrency, adversarial,
# persistence, recovery, protocol, multiprocess, and hardware-backed tests.
# NO CTest TIMEOUT properties are set anywhere in this file.

set(TESTS
  test_ids
  test_identity
  test_health
  test_capability
  test_eligibility
  test_observation
  test_registration
  test_drain_quarantine
  test_fleet_state
  test_protocol
  test_persistence
  test_recovery
)

foreach(t IN LISTS TESTS)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE gpufleet)
  target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR})
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Concurrency / property tests (fixed seeds, printed).
add_executable(test_property test_property.cpp)
target_link_libraries(test_property PRIVATE gpufleet)
target_include_directories(test_property PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME test_property COMMAND test_property)

# Multiprocess fleet proof (real coordinator + two worker OS processes).
if(GPUFLEETAGENT_BUILD_CLI)
  add_executable(test_multiprocess test_multiprocess.cpp)
  target_link_libraries(test_multiprocess PRIVATE gpufleet)
  target_include_directories(test_multiprocess PRIVATE ${CMAKE_SOURCE_DIR})
  add_test(NAME test_multiprocess
           COMMAND test_multiprocess $<TARGET_FILE:gpufleet_cli>)
endif()

# Real RTX 5090 CUDA validation proof (only when CUDA is built in).
if(GPUFLEETAGENT_HAVE_CUDA)
  add_executable(test_cuda_validation test_cuda_validation.cpp)
  target_link_libraries(test_cuda_validation PRIVATE gpufleet)
  target_include_directories(test_cuda_validation PRIVATE ${CMAKE_SOURCE_DIR})
  add_test(NAME test_cuda_validation COMMAND test_cuda_validation)
endif()
