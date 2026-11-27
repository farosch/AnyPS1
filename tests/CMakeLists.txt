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
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/fixture_loader.py
        --source ${FIXTURE_SOURCE_DIR}
        --output ${FIXTURE_OUTPUT_DIR}
    DEPENDS ${FIXTURE_JSON_FILES} ${CMAKE_SOURCE_DIR}/tests/fixture_loader.py
    VERBATIM
)
add_custom_target(generate_test_fixtures DEPENDS ${FIXTURE_MANIFEST})

add_test(NAME layout COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/check_layout.py)
add_test(NAME psx_exe_header COMMAND python3 -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")

add_executable(anyps1_relinker_test RelinkerTest.cpp)
target_include_directories(anyps1_relinker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_relinker_test PRIVATE anyps1_relinker)
add_dependencies(anyps1_relinker_test generate_test_fixtures)
add_test(NAME relinker_artifact COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/valid.ps-exe)
add_test(NAME relinker_native_subset COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-subset.ps-exe)
add_test(NAME relinker_native_memory COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-memory.ps-exe)
add_test(NAME relinker_native_host_integration COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-host-integration.ps-exe)
add_test(NAME relinker_native_bios_vector COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-bios-vector.ps-exe)
add_test(NAME relinker_native_load_delay COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-load-delay.ps-exe)
add_test(NAME relinker_native_cfg_cop0 COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-cfg-cop0.ps-exe)
add_test(NAME relinker_native_unaligned COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-unaligned.ps-exe)
add_test(NAME relinker_native_comprehensive COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/comprehensive.ps-exe)
add_test(NAME relinker_native_partial COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/mips/native-unsupported.ps-exe)
add_test(NAME relinker_invalid_fixture COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/invalid.ps-exe)
add_test(NAME relinker_truncated_fixture COMMAND anyps1_relinker_test ${FIXTURE_OUTPUT_DIR}/truncated.ps-exe)

find_program(SPIRV_VAL_EXECUTABLE NAMES spirv-val)
find_program(SPIRV_OPT_EXECUTABLE NAMES spirv-opt)
add_executable(anyps1_graphics_test GraphicsTest.cpp)
target_include_directories(anyps1_graphics_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps1_graphics_test PRIVATE anyps1_gpu)
add_test(NAME graphics_ir_spirv COMMAND anyps1_graphics_test)
set_tests_properties(graphics_ir_spirv PROPERTIES ENVIRONMENT
    "SPIRV_VAL_EXECUTABLE=${SPIRV_VAL_EXECUTABLE};SPIRV_OPT_EXECUTABLE=${SPIRV_OPT_EXECUTABLE}")

