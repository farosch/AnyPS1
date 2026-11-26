add_library(anyps1_relinker
    Relinker.cpp
    Runtime.cpp
    RuntimeGpu.inc
    RuntimeMemory.inc
    RuntimeMmio.inc
    RuntimeArith.inc
    HostVideo.cpp
    HostAudio.cpp
)

target_include_directories(anyps1_relinker
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(anyps1_relinker PUBLIC SDL3::SDL3)

