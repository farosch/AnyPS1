add_library(anyps1_runtime
    Runtime.cpp
    RuntimeGpu.inc
    RuntimeMemory.inc
    RuntimeMmio.inc
    RuntimeArith.inc
)

target_include_directories(anyps1_runtime
    PUBLIC ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(anyps1_runtime PUBLIC anyps1_host)
