# TODO

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
- [ ] Lower every MIPS I instruction used by PS-X executables:
  ADD/ADDU/SUB/SUBU, AND/OR/XOR/NOR, SLT/SLTU, shifts, variable shifts,
  ADDI/ADDIU/ANDI/ORI/XORI/LUI, SLTI/SLTIU, load/store byte/half/word,
  branches, J/JAL/JR/JALR, MULT/MULTU/DIV/DIVU, MFHI/MFLO/MTHI/MTLO,
  SYSCALL/BREAK, and unaligned load/store operations.
- [x] Add branch-delay-slot emission and PC-relative branch target lowering.
- [ ] Add load-delay-slot behavior for LB/LH/LW/LBU/LHU loads.
- [x] Implement arithmetic overflow and divide-by-zero exits in lowered ADD,
  ADDI, SUB, DIV, and DIVU paths.
- [ ] Define native control-flow graph lowering for multiple functions and
  indirect jumps instead of fixed-size entry/target blocks. Current work added
  CFG discovery scaffolding; enablement remains under test.
- [ ] Lower MIPS COP0 reads/writes where they correspond to host-platform state.
- [ ] Replace unsupported COP0/COP2 behavior with explicit conversion errors.
- [ ] Lower MIPS GTE/COP2 instructions directly or classify them as
  unsupported executable conversion features.
- [ ] Preserve the PS-X load address, stack pointer, global pointer, and entry
  point in native lowered code.

## Memory and address lowering

- [ ] Map PS1 RAM, scratchpad, and executable sections into native memory.
- [ ] Lower 32-bit PS1 addresses to native host addresses with checked bounds.
- [ ] Preserve endianness and 8/16/32-bit load/store behavior.
- [ ] Translate PS1 MMIO references into explicit unsupported conversion errors
  unless they have a documented native implementation.
- [ ] Remove all title-specific, BIOS-dependent, and virtualized-memory behavior
  from lowering.

## System calls and host integration

- [ ] Replace PS1 BIOS-vector assumptions with native host services or explicit
  unsupported conversion results.
- [ ] Document native equivalents for file, input, audio, video, and console APIs.
- [x] Add direct native I/O/exit handling.
- [ ] Do not require BIOS images or external PS-X state.

## Windows executable conversion

- [ ] Implement native PE32/PE32+ generation after Linux lowering is complete.
- [ ] Validate DOS/PE signatures, COFF headers, optional headers, sections,
  entrypoint, imports, relocations, and architecture.
- [ ] Test PE output with available Windows cross-toolchains and format inspectors.

## PS1 GPU lowering and Vulkan shaders

- [ ] Define a generic host graphics IR for PS1 GP0/GP1 commands and VRAM operations.
- [ ] Lower GP0 and GP1 state changes into Vulkan resources and command-buffer operations.
- [ ] Emit SPIR-V shaders for PS1 polygon, textured-polygon, sprite, rectangle,
  blending, and framebuffer operations.
- [ ] Add SPIR-V validation and optimization through `SPIRV-Tools` in conversion tests.
- [ ] Use `glslang` or a direct SPIR-V emitter for host shader generation.
- [ ] Add a Vulkan backend for Linux and Windows without adding NVIDIA/CUDA-only dependencies.
- [ ] Preserve unsupported GPU commands as explicit conversion failures.

## Validation

- [ ] Replace synthetic-only lowerer tests with minimal executable fixtures
  covering arithmetic, load/store, branches, calls, delay slots, and entrypoints.
- [x] Add negative fixtures for malformed and truncated PS-X headers.
- [ ] Relink all available reference games and report the exact first native
  lowering gap for each executable.
- [x] Add end-to-end tests that run a lowered executable and verify native exit
  status and observable host output.
- [ ] Add all conversion results to TODO/dev docs without recording interpretation
  behavior as a relinker requirement.
