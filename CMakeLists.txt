cmake_minimum_required(VERSION 3.20)

project(AnyPS1 LANGUAGES CXX)
include(GNUInstallDirs)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|MSVC")
    message(FATAL_ERROR
        "AnyPS1 requires Clang. Configure with cmake/toolchains/linux-clang-lld.cmake "
        "or cmake/toolchains/windows-clang-lld.cmake.")
endif()

if(WIN32)
    find_program(ANYPS1_LINKER lld-link REQUIRED)
else()
    find_program(ANYPS1_ASSEMBLER clang REQUIRED)
    find_program(ANYPS1_LINKER ld.lld REQUIRED)
    find_program(ANYPS1_ARCHIVER llvm-ar REQUIRED)
    find_program(ANYPS1_RANLIB llvm-ranlib REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(ANYPS1_STRICT_WARNINGS "Enable project compiler warnings" ON)
option(ANYPS1_BUILD_RELEASE_ARTIFACTS "Enable release packaging targets" ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")

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

if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "AnyPS1 requires SDL3 for controller, window, audio, and video support")
endif()

add_executable(anyps1
    src/main.cpp
)

target_include_directories(anyps1 PRIVATE src)
target_link_libraries(anyps1 PRIVATE anyps1_relinker)

if(WIN32)
    add_custom_command(TARGET anyps1 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:anyps1>")
elseif(UNIX AND NOT APPLE)
    set_target_properties(anyps1 PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib;$ORIGIN"
        INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN")
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(ANYPS1_BUILD_RELEASE_ARTIFACTS)
    include(CPack)
    set(CPACK_PACKAGE_NAME "AnyPS1")
    set(CPACK_PACKAGE_VENDOR "AnyPS1")
    set(CPACK_PACKAGE_VERSION "0.1.0")
    set(CPACK_GENERATOR "TGZ")
endif()

install(TARGETS anyps1
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(TARGET SDL3::SDL3)
    install(TARGETS SDL3-shared
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()


add_custom_target(progress
    COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/progress/generate_progress.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating AnyPS1 implementation progress report"
)

