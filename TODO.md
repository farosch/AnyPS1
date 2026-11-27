# TODO

## Linux executable conversion

- [ ] Complete native Linux ELF32/ELF64 executable generation with valid
  entrypoint, program headers, section headers, permissions, and dynamic
 /runtime linkage as required.
- [ ] Link each lowered executable with only host-native code and host-native
  data; never embed or invoke a PS1 interpreter.
- [ ] Add real Linux launch fixtures and verify them with `file`, `readelf`,
  execution, and exit status.

## MIPS instruction lowering

- [x] Add a direct lowering path for NOP and a tested arithmetic/control subset.
- [ ] Lower every MIPS I instruction used by PS-X executables:
  ADD/ADDU/SUB/SUBU, AND/OR/XOR/NOR, SLT/SLTU, shifts, variable shifts,
  ADDI/ADDIU/ANDI/ORI/XORI/LUI, SLTI/SLTIU, load/store byte/half/word,
  branches, J/JAL/JR/JALR, MULT/MULTU/DIV/DIVU, MFHI/MFLO/MTHI/MTLO,
  SYSCALL/BREAK, and unaligned load/store operations.
- [ ] Add load-delay-slot, branch-delay-slot, and PC-relative address semantics.
- [ ] Implement arithmetic overflow and divide-by-zero behavior matching MIPS I.
- [ ] Define native control-flow graph lowering for multiple functions and
  indirect jumps instead of fixed-size entry/target blocks.
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
- [ ] Add direct native I/O/exit handling without embedding executable PS1 services.
- [ ] Do not require BIOS images, PS-X runtime state, or PS1 MMIO interpretation.

## Windows executable conversion

- [ ] Implement native PE32/PE32+ generation after Linux lowering is complete.
- [ ] Validate DOS/PE signatures, COFF headers, optional headers, sections,
  entrypoint, imports, relocations, and architecture.
- [ ] Test PE output with available Windows cross-toolchains and format inspectors.

## Validation

- [ ] Replace synthetic-only lowerer tests with minimal executable fixtures
  covering arithmetic, load/store, branches, calls, delay slots, and entrypoints.
- [ ] Add negative fixtures for unsupported instructions and malformed headers.
- [ ] Relink all available reference games and report the exact first native
  lowering gap for each executable.
- [ ] Add end-to-end tests that run a lowered executable and verify native exit
  status and observable host output.
- [ ] Add all conversion results to TODO/dev docs without recording interpretation
  behavior as a relinker requirement.
