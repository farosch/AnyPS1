add_library(anyps1_host
    HostVideo.cpp
    HostAudio.cpp
)

target_include_directories(anyps1_host
    PUBLIC ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(anyps1_host PUBLIC SDL3::SDL3)
