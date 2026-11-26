# Usage

```sh
anyps1 <input.ps-exe> <output>
```

The relinker is currently a scaffold and does not yet convert executable images.

## Relink a PS-X EXE layout

```sh
anyps1 <input.ps-exe> <output.elf>
anyps1 --target linux <input.ps-exe> <output.elf>
anyps1 --target windows <input.ps-exe> <output.exe>
```

The first relinker stage writes an ELF32 MIPS layout artifact for the PS-X code
section. It does not translate PS1 MIPS instructions or claim native execution
support yet.

## Run a boot executable

```sh
anyps1 --run <input.ps-exe> <instruction-limit>
```

The native host executable uses the initial interpreter stage. It supports PS1
main RAM and scratchpad access, BIOS-vector returns, and core MIPS I instructions.

## Extract a PS1 CUE/BIN game

```sh
python3 tools/extract_psx_game.py "/path/to/Game.cue"
```

By default, extraction is written to `extracted/` beside the CUE sheet. Use `--output`
to select another directory.

The resulting layout is:

```text
extracted/
  disc1/
    filesystem/
    SYSTEM.CNF
    boot/
    game.json
  game.json
```

