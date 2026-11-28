add_library(anyps1_format
    format/PsExe.cpp
)

set(MIPS_OPCODE_REGISTRY_HEADER
    ${CMAKE_BINARY_DIR}/generated/MipsOpcodeRegistry.h)
add_custom_command(
    OUTPUT ${MIPS_OPCODE_REGISTRY_HEADER}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_mips_opcode_registry.py
        --registry ${CMAKE_SOURCE_DIR}/tools/mips_opcode_registry.json
        --output ${MIPS_OPCODE_REGISTRY_HEADER}
    DEPENDS
        ${CMAKE_SOURCE_DIR}/tools/mips_opcode_registry.json
        ${CMAKE_SOURCE_DIR}/tools/generate_mips_opcode_registry.py
    VERBATIM)
add_custom_target(generate_mips_opcode_registry
    DEPENDS ${MIPS_OPCODE_REGISTRY_HEADER})

add_library(anyps1_executable
    executable/ElfWriter.cpp
    executable/NativeProcess.cpp
    executable/PeWriter.cpp
)

target_compile_definitions(anyps1_executable PRIVATE
    ANYPS1_BUILD_DIR="${CMAKE_BINARY_DIR}")

add_library(anyps1_lowering
    lowering/MipsLowerer.cpp
    lowering/MipsLinuxLowerer.cpp
    lowering/MipsWindowsLowerer.cpp
    lowering/MipsLoweringSupport.cpp
    lowering/UnsupportedInstructions.cpp
    lowering/PayloadRegion.cpp
)
target_compile_definitions(anyps1_lowering PRIVATE
    ANYPS1_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_library(anyps1_gte
    lowering/GteRuntime.cpp
)
target_include_directories(anyps1_gte PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(anyps1_gte PRIVATE /GS- /Zl /EHs-c- /GR-)
    set_property(TARGET anyps1_gte PROPERTY
        VS_GLOBAL_BasicRuntimeChecks "Default")
endif()
add_dependencies(anyps1_lowering anyps1_gte)

add_library(anyps1_gpu
    gpu/Graphics.cpp
)

add_library(anyps1_input
    input/Input.cpp
)

add_library(anyps1_host
    host/HostServices.cpp
    host/NativeHostAbi.cpp
)

add_library(anyps1_device
    host/Ps1DeviceRuntime.cpp
)
target_include_directories(anyps1_device PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(anyps1_device PRIVATE /GS- /Zl /EHs-c- /GR-)
    set_property(TARGET anyps1_device PROPERTY
        VS_GLOBAL_BasicRuntimeChecks "Default")
endif()

add_library(anyps1_memory
    memory/MemoryMap.cpp
)

add_library(anyps1_relinker
    relinker/Relinker.cpp
)

foreach(target anyps1_format anyps1_executable anyps1_lowering anyps1_gpu anyps1_input anyps1_host anyps1_device anyps1_memory anyps1_relinker)
    target_include_directories(${target} PUBLIC ${CMAKE_SOURCE_DIR}/src)
endforeach()

if(ANYPS1_STRICT_WARNINGS)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        foreach(target anyps1_format anyps1_executable anyps1_lowering anyps1_gte
                        anyps1_gpu anyps1_input anyps1_host anyps1_device anyps1_memory
                        anyps1_relinker anyps1)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
            endif()
        endforeach()
    elseif(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        foreach(target anyps1_format anyps1_executable anyps1_lowering
                        anyps1_gpu anyps1_input anyps1_host anyps1_device anyps1_memory
                        anyps1_relinker anyps1)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE /W4 /WX)
            endif()
        endforeach()
    endif()
endif()

foreach(target anyps1_format anyps1_executable anyps1_lowering anyps1_gpu anyps1_input anyps1_host anyps1_relinker)
    target_include_directories(${target} PUBLIC ${CMAKE_BINARY_DIR}/generated)
    add_dependencies(${target} generate_mips_opcode_registry)
endforeach()

target_link_libraries(anyps1_input PUBLIC SDL3::SDL3)
target_link_libraries(anyps1_lowering PUBLIC anyps1_memory)
target_link_libraries(anyps1_host PUBLIC SDL3::SDL3)
target_link_libraries(anyps1_host PUBLIC anyps1_input)
target_link_libraries(anyps1_host PUBLIC anyps1_gpu)
target_link_libraries(anyps1_host PUBLIC anyps1_device)
target_include_directories(anyps1_gpu PUBLIC
    ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include)
target_link_libraries(anyps1_gpu PUBLIC SDL3::SDL3)

target_link_libraries(anyps1_lowering PUBLIC anyps1_format)
target_link_libraries(anyps1_lowering PUBLIC anyps1_executable)
target_link_libraries(anyps1_lowering PUBLIC anyps1_host)
target_link_libraries(anyps1_relinker
    PUBLIC anyps1_format anyps1_executable anyps1_lowering
)

