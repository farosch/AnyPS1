add_test(NAME layout COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_layout.py)
add_test(NAME psx_exe_header COMMAND python3 -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")

add_executable(anyps1_relinker_test RelinkerTest.cpp)
target_include_directories(anyps1_relinker_test PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(anyps1_relinker_test PRIVATE anyps1_relinker)
add_test(NAME relinker_artifact COMMAND anyps1_relinker_test ${CMAKE_SOURCE_DIR}/tests/fixtures/valid.ps-exe)

add_executable(anyps1_runtime_test RuntimeTest.cpp)
target_include_directories(anyps1_runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(anyps1_runtime_test PRIVATE anyps1_relinker)
add_test(NAME runtime_fixture_states COMMAND anyps1_runtime_test ${CMAKE_SOURCE_DIR}/tests/fixtures/runtime)

