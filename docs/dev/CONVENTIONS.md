# Code Conventions

- Keep implementation generic rather than title-specific.
- Throw or return an explicit error for unsupported formats and states.
- Add tests for format parsing and relinking behavior.
- Tests must use checked-in fixtures under `tests/fixtures/` for executable images,
  disc images, CUE files, headers, and other externally consumed file formats.
  Keep generated output artifacts temporary and never synthesize fixture input
  in test code.
- Fixtures must be minimal, reproducible, format-valid or deliberately malformed,
  and must not contain copyrighted game assets or real game binaries.
- Do not place third-party source trees directly into the repository.

