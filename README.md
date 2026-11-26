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
anyps1 --target linux "/path/to/PS-X.EXE" "/path/to/output.elf"
anyps1 --target windows "/path/to/PS-X.EXE" "/path/to/output.exe"
```

The first stage validates the PS-X header and writes an ELF32 MIPS layout
artifact. It does not yet translate MIPS instructions or produce a runnable
native Linux or Windows game executable.

## Run an extracted PS-X executable in the first runtime

```sh
anyps1 --run "/path/to/PS-X.EXE" 1000000
```

This generic interpreter provides 2 MiB main RAM, scratchpad and MMIO handling,
BIOS-vector returns, and MIPS I startup instructions. It is not yet a complete
game runtime and may stop at unsupported graphics, CD-ROM, controller, SPU, or
GTE behavior.

