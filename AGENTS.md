# AGENTS.md

AnyPS1 converts PlayStation 1 executable images into native Linux or Windows programs.

- Keep the relinker focused on executable conversion and avoid emulator runtime work.
- Unsupported executable formats and relinking states must fail explicitly.
- Functions must never fail silently; report or return errors through the
  appropriate exception, return value, log, or caller-visible result.
- Put third-party code under `3rdparty/<name>` as a submodule.
- Keep known format and platform gaps in `docs/dev/debt/`.
- Use Conventional Commit subjects.
- Build and test changes with CMake and CTest.

