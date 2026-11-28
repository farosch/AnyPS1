set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_AR llvm-lib)
set(CMAKE_RANLIB llvm-lib)
set(CMAKE_LINKER lld-link)
set(CMAKE_CXX_FLAGS_INIT "/std:c++17")
set(CMAKE_C_FLAGS_INIT "")

