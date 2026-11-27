cmake_minimum_required(VERSION 3.20)

project(AnyPS1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(ANYPS1_ENABLE_SPIRV_TOOLS "Build SPIR-V validation and optimization tools" OFF)
option(ANYPS1_ENABLE_GLSLANG "Build glslang shader tooling" OFF)

add_library(vulkan_headers INTERFACE)
target_include_directories(vulkan_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
)

if(ANYPS1_ENABLE_SPIRV_TOOLS)
    set(SPIRV-Headers_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SPIRV-Headers)
    set(SKIP_SPIRV_TOOLS_INSTALL ON)
    add_subdirectory(3rdparty/SPIRV-Tools)
endif()

if(ANYPS1_ENABLE_GLSLANG)
    if(NOT TARGET SPIRV-Tools-opt)
        message(FATAL_ERROR "ANYPS1_ENABLE_GLSLANG requires ANYPS1_ENABLE_SPIRV_TOOLS")
    endif()
    set(ALLOW_EXTERNAL_SPIRV_TOOLS ON)
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_ENABLE_INSTALL OFF)
    set(ENABLE_GLSLANG_BINARIES OFF)
    set(ENABLE_HLSL OFF)
    set(BUILD_EXTERNAL OFF)
    add_subdirectory(3rdparty/glslang)
endif()

if(NOT TARGET SDL3::SDL3)
    set(SDL_SHARED ON)
    set(SDL_STATIC OFF)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_TESTS OFF)
    add_subdirectory(3rdparty/SDL)
endif()

add_subdirectory(src)

add_executable(anyps1
    src/main.cpp
)

target_include_directories(anyps1 PRIVATE src)
target_link_libraries(anyps1 PRIVATE anyps1_relinker)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

