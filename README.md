# AnyPS1

AnyPS1 is a project for relinking PlayStation 1 game executables into native Linux or Windows programs.

This repository is an initial project scaffold. The executable format decoder and platform relinker are not implemented yet.

## Layout

- `core/relinker`: executable decoding and relinking implementation
- `core/libs`: platform and system-library compatibility modules
- `tools`: command-line and analysis utilities
- `cmake`: CMake helpers and dependency integration
- `docs`: developer and user documentation
- `demo`: web or native demonstrations
- `tests`: integration and unit tests
- `3rdparty`: third-party dependencies managed as submodules

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Extract a PS1 game

```sh
python3 tools/extract_psx_game.py "/path/to/Game.cue"
```

## Relink a PS-X EXE

```sh
anyps1 "/path/to/PS-X.EXE" "/path/to/output.elf"
```

The first stage validates the PS-X header and writes an ELF32 MIPS layout
artifact. It does not yet translate MIPS instructions or produce a runnable
native Linux or Windows game executable.

