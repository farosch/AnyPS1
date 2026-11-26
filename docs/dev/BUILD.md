# Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- Ninja or another CMake-supported build tool

Configure and build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

