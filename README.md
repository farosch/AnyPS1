# AnyPS1

AnyPS1 is a project for relinking PlayStation 1 game executables into native Linux or Windows programs.

This repository converts PS-X EXE images and extracted PlayStation 1 discs into
native Linux or Windows artifacts.

## Architecture

AnyPS1 is organized around executable conversion, direct MIPS lowering,
command-line tools, and target-platform output.

The implementation decodes PS-X EXE structures, analyzes references and entry
points, and produces native Linux or Windows artifacts through direct MIPS
instruction lowering. It does not execute or interpret PS-X behavior.

## Layout

- `core/relinker`: executable decoding and relinking implementation
- `core/anyps1.cpp`: main command-line relinker application
- `tools`: command-line and analysis utilities
- `TODO.md`: known implementation gaps and validation work
- `tests`: integration and unit tests
- `3rdparty`: third-party dependencies managed as submodules

## Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- Ninja or another CMake-supported build tool

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Extract a PS1 game

```sh
python3 tools/extract_psx_game.py "/path/to/Game.cue"
```

## Relink an extracted PS1 game

```sh
anyps1 "/path/to/extracted/game"
anyps1 --target linux "/path/to/extracted/game"
anyps1 --target windows "/path/to/extracted/game"
```

The relinker accepts an extracted game directory or disc directory. It locates
the boot executable from `SYSTEM.CNF` (or, as a fallback, a PS-X executable
header), lowers supported MIPS instructions directly into native Linux code,
and rejects unsupported instructions explicitly. It does not execute or interpret
PS-X behavior.

The resulting layout is:

```text
extracted/
  disc1/
    SYSTEM.CNF
    SCUS_941.63
    ... other extracted files
  disc2/
    SYSTEM.CNF
    SCUS_941.64
    ... other extracted files
```

Relinking a directory writes `game.elf` or `game.exe` into the directory itself:

```sh
anyps1 "/path/to/extracted"
anyps1 --target windows "/path/to/extracted/disc1"
```

