# GPU Fleet Agent examples.
set(EXAMPLES
  example_local_device_discovery
  example_capability_inspection
  example_execution_eligibility
  example_health_transition
  example_stale_observation
  example_drain_undrain
  example_quarantine_recovery
  example_worker_restart
  example_coordinator_epoch_rollover
  example_stale_authority_rejection
  example_persistence_recovery
  example_json_snapshot
)
foreach(e IN LISTS EXAMPLES)
  add_executable(${e} ${e}.cpp)
  target_link_libraries(${e} PRIVATE gpufleet)
  target_include_directories(${e} PRIVATE ${CMAKE_SOURCE_DIR})
endforeach()

if(GPUFLEETAGENT_HAVE_CUDA)
  add_executable(example_hardware_validation example_hardware_validation.cpp)
  target_link_libraries(example_hardware_validation PRIVATE gpufleet)
  target_include_directories(example_hardware_validation PRIVATE ${CMAKE_SOURCE_DIR})
endif()
