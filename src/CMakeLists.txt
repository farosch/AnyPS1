add_library(anyps1_format
    format/PsExe.cpp
)

add_library(anyps1_executable
    executable/PeWriter.cpp
)

add_library(anyps1_lowering
    lowering/MipsLowerer.cpp
)

add_library(anyps1_relinker
    relinker/Relinker.cpp
)

foreach(target anyps1_format anyps1_executable anyps1_lowering anyps1_relinker)
    target_include_directories(${target} PUBLIC ${CMAKE_SOURCE_DIR}/src)
endforeach()

target_link_libraries(anyps1_lowering PUBLIC anyps1_format)
target_link_libraries(anyps1_relinker
    PUBLIC anyps1_format anyps1_executable anyps1_lowering
)

