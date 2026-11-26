# Usage

```sh
anyps1 <input.ps-exe> <output>
```

The relinker is currently a scaffold and does not yet convert executable images.

## Extract a PS1 CUE/BIN game

```sh
python3 tools/extract_psx_game.py "/path/to/Game.cue"
```

By default, extraction is written to `extracted/` beside the CUE sheet. Use `--output`
to select another directory.

The resulting layout is:

```text
extracted/
  disc.iso
  filesystem/
  SYSTEM.CNF
  boot/
  game.json
```

