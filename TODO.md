# TODO

## Next relinker blockers

- [x] Remove the fixed 512-leader CFG limit and replace it with bounded CFG
  expansion that can preserve every emitted branch, call, and fallthrough target.
- [x] Prevent undefined assembly labels or duplicate JALR labels at CFG bounds;
  every out-of-range or unresolved transfer must emit an explicit
  unsupported-execution exit.
- [x] Count unsupported instructions only in statically reachable executable
  code, and distinguish those words from embedded data, tables, padding, or
  non-code payload regions.
- [x] Report native lowering coverage as a percentage and as exact leader/block,
  call-edge, and unresolved-indirect-transfer counts.
- [x] Replace the current real-executable smoke mechanism with an opt-in
  format-generic fixture/report test that does not depend on game names,
  directories, or assets.
- [x] Continue real executable relinking until the first reachable unsupported
  MIPS instruction or host operation is resolved, then add a minimal fixture
  that reproduces it without copyrighted assets.
- [x] Define native host mappings for file, input, console, audio, video, and
  Vulkan framebuffer operations, with unsupported services reported explicitly
  rather than silently emulated.
- [x] Provide generic integration surfaces for native launch, input polling,
  graphics IR lowering, Vulkan resource planning, and explicit host gaps.
- [x] Classify COP1 MIPS floating-point instructions and their load/store
  operations explicitly, and emit native unsupported-execution stubs because
  PS-X executable lowering has no COP1 floating-point runtime.
- [x] Implement PS-X GTE/COP2 matrix, vector, color, clipping, and compression
  operations with native host lowering or explicit per-function stubs.
- [x] Implement COP3, LWC0-LWC3, and SWC0-SWC3 instruction family stubs and
  classify unsupported reserved opcodes by exact opcode and SPECIAL function.

## Linux executable conversion

- [ ] Complete native Linux ELF32/ELF64 executable generation with valid
  entrypoint, program headers, section headers, permissions, and required
  host linking.
- [ ] Link each lowered executable with only host-native code and host-native
  data.
- [x] Add real Linux launch fixtures and verify them with `file`, `readelf`,
  execution, and exit status.
- [x] Add native executable terminal diagnostics with target, addresses, GP/SP,
  payload size, lowering size, and completion status.

## MIPS instruction lowering

- [x] Add a direct lowering path for NOP and a tested arithmetic/control subset.
- [x] Lower variable SLLV/SRLV/SRAV shifts and MTHI/MTLO in native entry and
  JAL target blocks.
- [x] Lower every MIPS I instruction used by PS-X executables:
  ADD/ADDU/SUB/SUBU, AND/OR/XOR/NOR, SLT/SLTU, shifts, variable shifts,
  ADDI/ADDIU/ANDI/ORI/XORI/LUI, SLTI/SLTIU, load/store byte/half/word,
  branches, J/JAL/JR/JALR, MULT/MULTU/DIV/DIVU, MFHI/MFLO/MTHI/MTLO,
  SYSCALL/BREAK, and unaligned load/store operations.
- [x] Add branch-delay-slot emission and PC-relative branch target lowering.
- [x] Add load-delay-slot behavior for LB/LH/LW/LBU/LHU loads.
- [x] Implement arithmetic overflow and divide-by-zero exits in lowered ADD,
  ADDI, SUB, DIV, and DIVU paths.
- [x] Add native control-flow graph discovery for entry blocks, static branch
  targets, jump targets, delay slots, and fallthrough leaders.
- [x] Complete CFG lowering for indirect jumps/JALR and multiple functions.
- [x] Lower MIPS COP0 reads/writes where they correspond to host-platform state.
- [x] Replace unsupported COP0/COP2 behavior with explicit conversion errors.
- [x] Lower MIPS GTE/COP2 instructions directly or classify them as
  unsupported executable conversion features.
- [x] Preserve the PS-X load address, stack pointer, global pointer, and entry
  point in native lowered code.
- [x] Emit a valid native ELF for partial lowerings and report the first
  unsupported instruction/address in terminal diagnostics.

## Memory and address lowering

- [x] Map PS1 RAM, scratchpad, and executable sections into native memory.
- [x] Lower 32-bit PS1 addresses to native host addresses with checked bounds.
- [x] Preserve endianness and 8/16/32-bit load/store behavior.
- [x] Translate PS1 MMIO references into explicit unsupported conversion errors
  unless they have a documented native implementation.
- [x] Remove all title-specific, BIOS-dependent, and virtualized-memory behavior
  from lowering.

## System calls and host integration

- [x] Replace PS1 BIOS-vector assumptions with explicit unsupported conversion
  results or native host services.
- [x] Document native equivalents for file, input, audio, video, and console APIs.
- [x] Add direct native I/O/exit handling.
- [x] Do not require BIOS images or external PS-X state.
- [x] Implement SDL-mapped PS1 controller input, including analog sticks and
  triggers.
- [x] Implement keyboard and mouse host input mappings.
- [x] Add an `anyps1-input.ini` format for configuring controller, keyboard,
  and mouse mappings.

## Windows executable conversion

- [x] Implement native PE32+ generation with explicit Windows lowering diagnostics.
- [x] Validate DOS/PE signatures, COFF headers, optional headers, sections,
  entrypoint, imports, relocations, and architecture.
- [x] Test PE output with available Windows cross-toolchains and format inspectors.

## PS1 GPU lowering and Vulkan shaders

- [x] Define a generic host graphics IR for PS1 GP0/GP1 commands and VRAM operations.
- [x] Lower GP0 and GP1 state changes into Vulkan resources and command-buffer operations.
- [x] Emit SPIR-V shaders for PS1 polygon, textured-polygon, sprite, rectangle,
  blending, and framebuffer operations.
- [x] Add SPIR-V validation and optimization through `SPIRV-Tools` in conversion tests.
- [x] Use `glslang` or a direct SPIR-V emitter for host shader generation.
- [x] Add a Vulkan backend for Linux and Windows without adding NVIDIA/CUDA-only dependencies.
- [x] Preserve unsupported GPU commands as explicit conversion failures.

## Validation

- [x] Replace synthetic-only lowerer tests with minimal executable fixtures
  covering arithmetic, load/store, branches, calls, delay slots, and entrypoints.
- [x] Add negative fixtures for malformed and truncated PS-X headers.
- [x] Relink all available reference games and report the exact first native
  lowering gap for each executable. Run
  `python3 tools/report_relink_gaps.py --binary build/anyps1 --reference-root references/games --output build/reference-relink-report.txt`
  as an opt-in smoke operation; it must not become a title-specific CTest or relinker requirement.
- [x] Add end-to-end tests that run a lowered executable and verify native exit
  status and observable host output.
- [x] Add all conversion results to TODO/dev docs without recording interpretation
  behavior as a relinker requirement.
