add_library(anyps1_relinker
    Relinker.cpp
    Runtime.cpp
)

target_include_directories(anyps1_relinker
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

