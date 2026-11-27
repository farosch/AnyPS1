cmake_minimum_required(VERSION 3.20)

project(AnyPS1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_subdirectory(core/relinker)

add_executable(anyps1
    core/anyps1.cpp
)

target_include_directories(anyps1 PRIVATE core)
target_link_libraries(anyps1 PRIVATE anyps1_relinker)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

