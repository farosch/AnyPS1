# Conventions

- Use C++17 unless a compatibility target requires otherwise.
- Keep public APIs in corresponding headers beside implementation files.
- Command-line tools return explicit exit codes and report errors to stderr.
- Third-party code belongs in `3rdparty/` as a submodule.
- Record unsupported behavior rather than silently returning success.
- Tests must use checked-in fixtures under `tests/fixtures/` for file-format
  inputs and external data; generated outputs remain temporary.

