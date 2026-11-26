# AGENTS.md

AnyPS1 converts PlayStation 1 executable images into native Linux or Windows programs.

- Keep the relinker focused on executable conversion and avoid emulator runtime work.
- Unsupported executable formats and relinking states must fail explicitly.
- Functions must never fail silently; report or return errors through the
  appropriate exception, return value, log, or caller-visible result.
- Put third-party code under `3rdparty/<name>` as a submodule.
- Keep known format and platform gaps in `TODO.md`.
- Use Conventional Commit subjects.
- Build and test changes with CMake and CTest.

## Conventions

- Use C++17 unless a compatibility target requires otherwise.
- Keep public APIs in corresponding headers beside implementation files.
- Command-line tools return explicit exit codes and report errors to stderr.
- Keep implementation generic rather than title-specific.
- Add tests for format parsing and relinking behavior.
- Tests must use checked-in fixtures under `tests/fixtures/` for executable,
  disc, header, CUE, or other external file-format input. Generated outputs
  remain temporary and fixture input must not be synthesized in test code.
- Fixture files must be minimal, reproducible, format-valid or deliberately
  malformed, and must not contain copyrighted game assets or real game binaries.
- Record unsupported behavior rather than silently returning success.

## Contributing

1. Create a focused topic branch.
2. Follow the repository layout and conventions above.
3. Add or update checked-in fixtures for tests that consume executable, disc,
   header, CUE, or other external file data.
4. Build and run tests before submitting changes.
5. Document format or platform gaps in `TODO.md`.

