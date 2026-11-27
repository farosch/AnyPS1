set(FIXTURE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/fixtures/json)
set(FIXTURE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tests/fixtures)
set(FIXTURE_MANIFEST ${FIXTURE_OUTPUT_DIR}/fixture-manifest.json)

file(GLOB FIXTURE_JSON_FILES CONFIGURE_DEPENDS
    ${FIXTURE_SOURCE_DIR}/*.json
    ${FIXTURE_SOURCE_DIR}/*/*.json
    ${FIXTURE_SOURCE_DIR}/*/*/*.json
)
add_custom_command(
    OUTPUT ${FIXTURE_MANIFEST}
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tests/fixture_loader.py
        --source ${FIXTURE_SOURCE_DIR}
        --output ${FIXTURE_OUTPUT_DIR}
    DEPENDS ${FIXTURE_JSON_FILES} ${CMAKE_SOURCE_DIR}/tests/fixture_loader.py
    VERBATIM
)
add_custom_target(generate_test_fixtures DEPENDS ${FIXTURE_MANIFEST})

add_test(NAME layout COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tests/check_layout.py)
add_test(NAME psx_exe_header COMMAND "${PYTHON_EXECUTABLE}" -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")
add_test(NAME lowering_inventory
    COMMAND "${PYTHON_EXECUTABLE}"
        ${CMAKE_SOURCE_DIR}/tests/test_target_parity.py)

add_executable(anyps1_relinker_test RelinkerTest.cpp)
target_include_directories(anyps1_relinker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_relinker_test PRIVATE anyps1_relinker)
add_dependencies(anyps1_relinker_test generate_test_fixtures)
add_test(NAME relinker_artifact COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/valid.ps-exe)
add_test(NAME relinker_native_subset COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-subset.ps-exe)
add_test(NAME relinker_native_memory COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-memory.ps-exe)
add_test(NAME relinker_native_ram_mirror COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-ram-mirror.ps-exe)
add_test(NAME relinker_native_host_integration COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-host-integration.ps-exe)
add_test(NAME relinker_native_device_timer COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-device-timer.ps-exe)
add_test(NAME relinker_native_bios_vector COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-bios-vector.ps-exe)
add_test(NAME relinker_native_bios_memcpy COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-bios-memcpy.ps-exe)
add_test(NAME relinker_native_load_delay COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-load-delay.ps-exe)
add_test(NAME relinker_zero_register COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/zero-register.ps-exe)
add_test(NAME relinker_taken_delay_slot COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/delay-slot-taken.ps-exe)
add_test(NAME relinker_delay_slot_data_boundary COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/delay-slot-data-boundary.ps-exe)
add_test(NAME relinker_memory_boundary COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/memory-boundary.ps-exe)
add_test(NAME relinker_platform_parity
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/platform-parity.ps-exe)
add_test(NAME relinker_native_cfg_cop0 COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-cfg-cop0.ps-exe)
add_test(NAME relinker_native_unaligned COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-unaligned.ps-exe)
add_test(NAME relinker_native_comprehensive COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/comprehensive.ps-exe)
add_test(NAME relinker_native_partial COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-unsupported.ps-exe)
add_test(NAME relinker_invalid_fixture COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/invalid.ps-exe)
add_test(NAME relinker_truncated_fixture COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/truncated.ps-exe)
set_tests_properties(
    relinker_artifact
    relinker_native_subset
    relinker_native_memory
    relinker_native_ram_mirror
    relinker_native_host_integration
    relinker_native_device_timer
    relinker_native_bios_vector
    relinker_native_bios_memcpy
    relinker_native_load_delay
    relinker_zero_register
    relinker_taken_delay_slot
    relinker_delay_slot_data_boundary
    relinker_memory_boundary
    relinker_platform_parity
    relinker_native_cfg_cop0
    relinker_native_unaligned
    relinker_native_comprehensive
    relinker_native_partial
    relinker_invalid_fixture
    relinker_truncated_fixture
    PROPERTIES
        ENVIRONMENT "ANYPS1_TEST_HEADLESS=1"
        TIMEOUT 15)

find_program(SPIRV_VAL_EXECUTABLE NAMES spirv-val)
find_program(SPIRV_OPT_EXECUTABLE NAMES spirv-opt)
add_executable(anyps1_graphics_test GraphicsTest.cpp)
target_include_directories(anyps1_graphics_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_graphics_test PRIVATE anyps1_gpu)
add_test(NAME graphics_ir_spirv COMMAND anyps1_graphics_test)
set_tests_properties(graphics_ir_spirv PROPERTIES ENVIRONMENT
    "SPIRV_VAL_EXECUTABLE=${SPIRV_VAL_EXECUTABLE};SPIRV_OPT_EXECUTABLE=${SPIRV_OPT_EXECUTABLE}")

add_executable(anyps1_input_test InputTest.cpp)
target_include_directories(anyps1_input_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_input_test PRIVATE anyps1_input)
add_test(NAME input_mapping
    COMMAND anyps1_input_test ${CMAKE_SOURCE_DIR}/tests/fixtures/anyps1-input.ini)

add_executable(anyps1_host_integration_test HostIntegrationTest.cpp)
target_include_directories(anyps1_host_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_host_integration_test
    PRIVATE anyps1_host anyps1_input anyps1_gpu)
add_test(NAME host_integration_ir
    COMMAND anyps1_host_integration_test)

add_test(NAME unsupported_instruction_inventory
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/inventory_unsupported_instructions.py
        ${FIXTURE_OUTPUT_DIR}/mips/native-unsupported.ps-exe
        --output ${CMAKE_BINARY_DIR}/unsupported-instruction-inventory.json)

add_executable(anyps1_unsupported_instruction_test
    UnsupportedInstructionsTest.cpp)
target_include_directories(anyps1_unsupported_instruction_test
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_unsupported_instruction_test
    PRIVATE anyps1_lowering)
add_test(NAME unsupported_instruction_registry
    COMMAND anyps1_unsupported_instruction_test)

add_test(NAME pe_format_inspector
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/valid.ps-exe)

add_test(NAME pe_format_comprehensive
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/comprehensive.ps-exe)

add_test(NAME pe_format_partial
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-unsupported.ps-exe)
add_test(NAME pe_semantic_load_delay
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-load-delay.ps-exe)
add_test(NAME pe_semantic_zero_register
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/zero-register.ps-exe)
add_test(NAME pe_semantic_taken_delay_slot
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/delay-slot-taken.ps-exe)
add_test(NAME pe_delay_slot_data_boundary
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/delay-slot-data-boundary.ps-exe)
add_test(NAME pe_host_mmio_state
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-host-integration.ps-exe)
add_test(NAME pe_device_timer
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-device-timer.ps-exe)
add_test(NAME pe_native_bios_memcpy
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-bios-memcpy.ps-exe)
add_test(NAME pe_memory_boundary
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/memory-boundary.ps-exe)
add_test(NAME pe_native_ram_mirror
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-ram-mirror.ps-exe)
add_test(NAME pe_platform_parity
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/platform-parity.ps-exe)
add_test(NAME pe_cop1_parity
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-cop1.ps-exe)
add_test(NAME pe_cop1_edge_parity
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-cop1-edge-cases.ps-exe)
add_test(NAME pe_unaligned_parity
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-unaligned.ps-exe)

add_test(NAME relinker_static_jalr
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-static-jalr.ps-exe)
add_test(NAME relinker_dynamic_jalr
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-dynamic-jalr.ps-exe)
add_test(NAME pe_dynamic_jalr
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/check_pe.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/mips/native-dynamic-jalr.ps-exe)

add_test(NAME relinker_native_cop1
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-cop1.ps-exe)

add_test(NAME relinker_native_cop1_edge_cases
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-cop1-edge-cases.ps-exe)

add_test(NAME relinker_native_a0_file_calls
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-a0-file-calls.ps-exe)

add_test(NAME relinker_native_cop2_transfers
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-cop2-transfers.ps-exe)

add_test(NAME relinker_load_family_delay
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/load-family-delay.ps-exe)

add_test(NAME relinker_sparse_cfg
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-sparse-cfg.ps-exe)

add_test(NAME relinker_mips_edge_cases
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/mips-edge-cases.ps-exe)
add_test(NAME relinker_native_likely_branches
    COMMAND anyps1_relinker_test
        ${FIXTURE_OUTPUT_DIR}/mips/native-likely-branches.ps-exe)


add_executable(anyps1_payload_region_test PayloadRegionTest.cpp)
target_include_directories(anyps1_payload_region_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_payload_region_test PRIVATE anyps1_lowering)
add_test(NAME payload_region_classifier
    COMMAND anyps1_payload_region_test)

add_executable(anyps1_mips_lowering_support_test
    MipsLoweringSupportTest.cpp)
target_include_directories(anyps1_mips_lowering_support_test
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_mips_lowering_support_test
    PRIVATE anyps1_lowering)
add_test(NAME mips_lowering_support
    COMMAND anyps1_mips_lowering_support_test)

add_test(NAME validation_tools
    COMMAND "${PYTHON_EXECUTABLE}"
        ${CMAKE_SOURCE_DIR}/tests/test_validation_tools.py)
set_tests_properties(validation_tools PROPERTIES TIMEOUT 30)

add_test(NAME cfg_timeout
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/validate_cfg_timeout.py
        --relinker $<TARGET_FILE:anyps1>
        --fixture ${FIXTURE_OUTPUT_DIR}/valid.ps-exe
        --timeout 10)
set_tests_properties(cfg_timeout PROPERTIES TIMEOUT 15)

add_executable(anyps1_memory_map_test MemoryMapTest.cpp)
target_include_directories(anyps1_memory_map_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_memory_map_test PRIVATE anyps1_memory)
add_test(NAME memory_map_mmio_dma
    COMMAND anyps1_memory_map_test)

add_executable(anyps1_gte_runtime_test GteRuntimeTest.cpp)
target_include_directories(anyps1_gte_runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_gte_runtime_test PRIVATE anyps1_gte)
add_test(NAME gte_runtime COMMAND anyps1_gte_runtime_test)

