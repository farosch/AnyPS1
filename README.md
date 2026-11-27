# AnyPS1

AnyPS1 converts extracted PlayStation 1 game files into native Linux or Windows
programs. It is a relinker and PS1 executable conversion project, not a PS1
emulator.

The required relinker input is an extracted game folder prepared in the layout
described below. A raw CUE/BIN disc is not the relinker input. The extraction
script in this repository is a helper that turns CUE/BIN discs into that
expected extracted-folder layout.

## Workflow

```text
CUE/BIN discs
    |
    v
tools/extract_psx_game.py
    |
    v
extracted game folder
    |
    v
anyps1
    |
    v
native game ELF or PE executable
```

1. Extract one or more CUE/BIN discs with `tools/extract_psx_game.py`.
2. Point `anyps1` at the resulting extracted disc folder or game folder.
3. The relinker reads `SYSTEM.CNF`, finds the boot PS-X executable, lowers
   supported MIPS code, and writes the native artifact beside the extracted
   game files.

## Extraction helper

Extract a single-disc game:

```sh
python3 tools/extract_psx_game.py "/path/to/Game.cue"
```

Extract every disc of a multi-disc game:

```sh
python3 tools/extract_psx_game.py \
  "/path/to/Game (Disc 1).cue" \
  "/path/to/Game (Disc 2).cue" \
  "/path/to/Game (Disc 3).cue"
```

The default output is an `extracted/` directory next to the first CUE file.
Use `--output` to choose another location:

```sh
python3 tools/extract_psx_game.py \
  "/path/to/Game (Disc 1).cue" \
  "/path/to/Game (Disc 2).cue" \
  --output "/path/to/output"
```

The extraction helper removes and recreates each output disc directory on
subsequent runs.

The resulting layout is direct and contains the extracted disc filesystem:

```text
extracted/
  disc1/
    SYSTEM.CNF
    SCUS_941.63
    BATTLE/
    MENU/
    ...other extracted files
  disc2/
    SYSTEM.CNF
    SCUS_941.64
    ...other extracted files
```

When the game has one disc, `anyps1` can read `extracted/disc1/`. For a
multi-disc game, point `anyps1` at the relevant disc folder or at the overall
`extracted/` folder.

## Relinking

Build the project, then pass the extracted folder:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/anyps1 "/path/to/extracted/disc1"
```

The default target is Linux:

```sh
./build/anyps1 --target linux "/path/to/extracted/disc1"
```

Windows conversion is a planned target:

```sh
./build/anyps1 --target windows "/path/to/extracted/disc1"
```

The relinker:

- accepts an extracted disc folder or an extracted game folder;
- reads `SYSTEM.CNF` to identify the boot executable;
- falls back to locating a PS-X EXE header in the extracted files;
- writes `game.elf` for Linux or `game.exe` for Windows in the directory supplied
  as input;
- reports unsupported executable layouts or instructions explicitly.

Native host integration:

- Console output maps to Linux `write(2)` and is emitted from `_start`.
- Process exit maps to Linux `exit(2)`; BREAK uses exit status `9`.
- SYSCALL is the native lowerer's stop point, not a PS1 BIOS call.
- PS1 BIOS A0/B0/C0 vectors are rejected because they are not native host APIs.
- File access must be converted to Linux `open/read/write/close` operations.
- Input must be converted to Linux input APIs or explicit unsupported results.
- Audio and video must use native host APIs, not PS1 controller/SPU/MMIO emulation.

Do not pass `Game.cue`, `Game.bin`, or an arbitrary PS-X executable to the
normal directory workflow. The helper and relinker accept different inputs.

## GPU lowering and Vulkan

PS1 GP0/GP1 commands and framebuffer operations are planned for lowering into a
generic host graphics intermediate representation. Vulkan is the intended
cross-platform rendering target because it supports portable SPIR-V shaders.
CUDA is not used because it is NVIDIA-specific.

The repository includes:

- `3rdparty/Vulkan-Headers`
- `3rdparty/SPIRV-Headers`
- `3rdparty/SPIRV-Tools`
- `3rdparty/glslang`

Enable SPIR-V tooling explicitly:

```sh
cmake -S . -B build -G Ninja \
  -DANYPS1_ENABLE_SPIRV_TOOLS=ON \
  -DANYPS1_ENABLE_GLSLANG=ON
```

GPU lowering is not complete. Unsupported GPU commands must fail explicitly
and must not silently disappear or fall back to an emulator-style runtime.

## Layout

- `src/`: relinker and executable-lowering implementation
- `tools/`: extraction and PS1 analysis helpers
- `tests/`: fixture-based C++ and Python tests
- `assets/`: repository assets such as the relinker icon
- `3rdparty/`: third-party dependencies managed as submodules
- `TODO.md`: implementation gaps and validation work

## Testing

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests materialize generated binary inputs from JSON descriptions under
`tests/fixtures/json/`. Generated `.ps-exe` and `.bin` files remain build
artifacts and are not source-controlled fixtures.

## Reference conversion validation

The `tools/report_relink_gaps.py` helper performs format-generic smoke validation
against locally extracted reference executables discovered through `SYSTEM.CNF`.
It reports each executable's first conversion gap without embedding title,
platform, or asset-specific behavior in the relinker or tests.
