cmake_minimum_required(VERSION 3.20)

project(AnyPS1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(SDL_TESTS OFF CACHE BOOL "Build SDL tests")
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library")
set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL examples")
set(SDL_INSTALL OFF CACHE BOOL "Install SDL")
add_subdirectory(3rdparty/SDL)

add_subdirectory(core/relinker)

add_executable(anyps1
    tools/anyps1.cpp
)

target_include_directories(anyps1 PRIVATE core)
target_link_libraries(anyps1 PRIVATE anyps1_relinker)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

