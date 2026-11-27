# TODO

## Current relinker blockers

- [ ] Replace partial Linux lowering with complete native lowering for every
  statically reachable PS-X instruction and host operation.
- [x] Remove the legacy fixed-block `write_linux_native` path and make
  `write_linux_native_cfg` the only Linux lowering implementation.
- [x] Improve CFG discovery beyond static leaders. Resolve JALR targets when
  they are statically provable, preserve indirect-call edges, and emit explicit
  unsupported exits for unresolved indirect control flow.
- [x] Distinguish executable code from embedded data, tables, padding,
  `.rodata`, and payload regions when counting unsupported instructions.
- [x] Report exact supported/reachable word counts, unsupported word counts,
  leader counts, call-edge counts, unresolved-indirect counts, and the first
  unsupported address without including data words as instructions.
- [x] Add per-instruction native lowering fixtures for every MIPS I opcode,
  COP0 operation, COP1 operation, COP2/GTE operation, COP3 operation,
  coprocessor load/store, and reserved opcode.
- [x] Remove the remaining generic reserved-opcode and coprocessor stubs that
  can be lowered directly, and keep only irreducibly unsupported operations
  as explicit conversion failures.
- [x] Replace substring-based unsupported-instruction heuristics with a shared
  opcode/classification implementation used by lowering, disassembly,
  diagnostics, and inventory tools.

## MIPS and COP lowering

### Implemented

- [x] Add direct native lowering for NOP and the arithmetic/control subset.
- [x] Lower fixed and variable shifts.
- [x] Lower MIPS I arithmetic, logical, compare, immediate, branch, and jump
  instructions.
- [x] Lower byte, halfword, word, and unaligned load/store instructions.
- [x] Lower MULT/MULTU/DIV/DIVU and HI/LO operations.
- [x] Lower MTHI/MTLO and MFHI/MFLO.
- [x] Lower SYSCALL and BREAK to native process exit behavior.
- [x] Add PC-relative branch and static J/JAL lowering.
- [x] Add JR/JALR emission and JALR target deduplication.
- [x] Add branch-delay-slot emission and load-delay behavior.
- [x] Implement signed ADD/ADDI/SUB overflow exits.
- [x] Implement divide-by-zero exits.
- [x] Preserve load address, entrypoint, GP, SP, RAM, and scratchpad state.
- [x] Lower checked PS1 COP0 reads/writes where they correspond to host state.
- [x] Add explicit COP0/COP1/COP2/COP3 and coprocessor load/store stubs.
- [x] Name known GTE/COP2 operations and add exact GTE test coverage.
- [x] Classify exact reserved opcodes and SPECIAL functions.
- [x] Inventory unsupported instruction words and families across executable
  payloads.

### Remaining

- [x] Add native COP1 MFC1/MTC1/CFC1/CTC1, add/sub/mul/div/sqrt, Cvt.S.W,
  Cvt.W.S, C.EQ/LT/LE, BC1 branches, and checked LWC1/SWC1 memory paths.
- [ ] Add remaining COP1 conversions/comparison variants and exhaustive
  floating-point edge-path fixtures.
- [x] Classify COP2/GTE instructions and named GTE operations with exact
  unsupported stubs; native matrix/vector/color lowering remains open.
- [x] Implement COP3 opcode classification and exact unsupported execution
  stubs.
- [x] Implement the native COP1 LWC1/SWC1 subset and add checked unsupported
  stubs for LWC0-LWC3/SWC0-SWC3 coprocessor families.
- [x] Complete load-delay behavior for every MIPS load form and verify the
  delay-slot fixtures against real instruction sequences.
- [ ] Verify exact MIPS signed/unsigned overflow, division edge cases,
  endianness, unaligned access, and HI/LO semantics with focused fixtures.
- [ ] Add support for sparse control-flow regions and discontinuous executable
  sections in CFG discovery.

## Linux executable conversion

### Implemented

- [x] Add an explicit `--target linux|windows` parameter.
- [x] Generate ELF64 x86-64 executables with a native `_start` entrypoint.
- [x] Emit valid ELF entry, program-header, section-header, text, rodata,
  and BSS metadata.
- [x] Link with `gcc -nostdlib -static -no-pie`.
- [x] Add no-exec-stack, RELRO, now, notext, and build-ID linker options.
- [x] Validate ELF signature, ELF64 class, little-endian encoding, x86-64
  machine type, ET_EXEC type, entrypoint, program headers, and section headers.
- [x] Add `file`, `readelf`, execution, and exit-status fixture coverage.
- [x] Add comprehensive terminal diagnostics for target, addresses, GP/SP,
  payload size, lowering size, coverage, call edges, unresolved transfers,
  unsupported counts, and completion status.
- [x] Produce valid partial ELF artifacts for unsupported instructions.
- [x] Exit at unsupported instructions with a dedicated native status and
  first-gap diagnostic.
- [x] Keep native executable permissions on generated Linux artifacts.

### Remaining

- [ ] Remove all legacy assembly lowering and fixed-block code duplication.
- [x] Eliminate shell-string linker invocation and use a repository-owned
  argument-safe toolchain API.
- [ ] Add ELF32, ARM64, and other architecture targets only after their native
  lowering backends exist.
- [ ] Add PIE/ASLR support only if it is compatible with preserved PS1 addresses.
- [ ] Verify generated ELF program-header permissions and file offsets for
  text, rodata, bss, payload, and symbol tables.
- [ ] Verify generated ELF artifacts with `readelf`, `file`, a linker validator,
  and direct launch in every supported Linux build environment.
- [ ] Emit host-native sections only; reject or exclude PS1 payload bytes from
  executable state that are not referenced by lowered native code.

## Memory and address lowering

### Implemented

- [x] Map 2 MiB PS1 RAM and 1 KiB scratchpad into native memory.
- [x] Normalize 32-bit PS1 addresses to host RAM/scratchpad offsets.
- [x] Preserve 8/16/32-bit load/store behavior and signed/unsigned extensions.
- [x] Emit checked bounds for RAM and scratchpad accesses.
- [x] Route out-of-range and MMIO accesses to explicit fault/unsupported exits.
- [x] Preserve executable payload bytes for native payload copying.
- [x] Remove title-specific, BIOS-dependent, and virtualized-memory behavior.

### Remaining

- [ ] Implement page/range validation for executable sections and RAM boundaries.
- [ ] Add executable-section protection and separate code/data/rodata mappings.
- [ ] Add a generic MMIO map with documented native implementations.
- [ ] Lower PS1 DMA to native host memory copies or explicit unsupported exits.
- [ ] Lower CD-ROM, disc, cache, timers, controllers, memory cards, SPU, GPU,
  interrupt, and peripheral state without title-specific behavior.
- [ ] Implement 32-bit address wraparound consistently across every load,
  store, branch, jump, and call calculation.
- [ ] Add exhaustive unaligned load/store and endianness fixtures.
- [ ] Reject BIOS-dependent executable transitions explicitly.

## Host integration

### Implemented

- [x] Replace BIOS-vector assumptions with native host services or explicit
  unsupported results.
- [x] Add native Linux console write and process exit handling.
- [x] Implement generic file, input, audio, video, and Vulkan host mappings.
- [x] Add SDL3 gamepad, keyboard, and mouse runtime support.
- [x] Add analog sticks, triggers, shoulders, D-pad, and face buttons.
- [x] Add `anyps1-input.ini` parsing and default bindings.
- [x] Add host mapping and input integration fixtures.
- [x] Add unsupported MMIO and BIOS-vector fixtures.

### Remaining

- [ ] Implement native file lowering for open/read/write/seek/close operations.
- [ ] Integrate input polling into the actual generated executable loop.
- [ ] Implement SDL3 window creation and event pumping in the Linux/Windows
  host backend.
- [ ] Implement native audio lowering for PS1 SPU operations.
- [ ] Implement native video framebuffer presentation.
- [ ] Implement native Vulkan swapchain, image, sampler, pipeline, and
  command-buffer resources.
- [ ] Expose host-native diagnostics through structured exit codes and logs.
- [ ] Implement title-independent memory-card storage mapping.
- [ ] Implement title-independent CD/disc filesystem and asset access mapping.
- [ ] Add explicit unsupported exits for every unimplemented BIOS vector and
  MMIO device family.

## Windows executable conversion

### Implemented

- [x] Generate PE32+ layout for AMD64 with DOS and PE signatures.
- [x] Add COFF headers, optional headers, text/rdata sections, entrypoint,
  imports, relocations, architecture fields, and host diagnostics.
- [x] Add PE signature, machine, section, optional-header, and import-directory
  fixture validation.
- [x] Add `objdump`/`llvm-readobj` format-inspector support.
- [x] Use explicit Windows lowering diagnostics rather than silent success.

### Remaining

- [ ] Implement native Windows MIPS lowering equivalent to Linux lowering.
- [ ] Generate imports and relocations for real Windows host-native code.
- [ ] Add PE32 support if an x86 compatibility target is required.
- [ ] Test generated PE32+ output with available Windows cross-toolchains.
- [ ] Add Windows execution fixtures and exit-status/diagnostic validation.
- [ ] Integrate Windows SDL3 input and Vulkan host services.
- [ ] Validate generated PE images with platform format inspectors and signed
  executable tooling as applicable.

## PS1 GPU lowering and Vulkan shaders

### Implemented

- [x] Define a generic GP0/GP1 and VRAM graphics IR.
- [x] Lower GP0/GP1 state changes into Vulkan resource plans.
- [x] Emit direct SPIR-V shader modules for polygon, textured-polygon, sprite,
  rectangle, and framebuffer primitive categories.
- [x] Add generic SPIR-V validation and optimization test support.
- [x] Add Vulkan/Linux and Vulkan/Windows platform enums without CUDA/NVIDIA
  dependencies.
- [x] Preserve unsupported GPU commands as explicit conversion failures.

### Remaining

- [ ] Model GP0/GP1 command payloads completely, including VRAM transfers,
  draw areas, texture windows, display areas, display modes, DMA direction,
  and display enable state.
- [ ] Emit primitive-specific SPIR-V shaders rather than a shared minimal module.
- [ ] Implement PS1 blending modes, semi-transparency, dithering, 16/24-bit
  color, CLUT/texture lookup, texture-page modes, and framebuffer reads.
- [ ] Implement polygon, textured-polygon, sprite, rectangle, line, and
  framebuffer operations with native Vulkan pipelines.
- [ ] Implement VRAM-to-host-image and host-image-to-VRAM transfers.
- [ ] Create and validate Vulkan resources in the Linux/Windows runtime.
- [ ] Integrate shader validation and optimization into release builds.
- [ ] Add SPIR-V golden fixtures and malformed GPU command fixtures.
- [ ] Preserve unsupported GPU commands as conversion failures, including no
  silent drop or emulator fallback.

## Unsupported instruction tracking

### Implemented

- [x] Add a C++ unsupported-instruction classifier for SPECIAL, COP0/COP1/COP2/
  COP3, coprocessor load/store, and reserved opcodes.
- [x] Add exact GTE/COP2 function names for known operations.
- [x] Emit category-specific native unsupported stubs.
- [x] Add first-gap, unsupported-count, coverage, call-edge, and unresolved
  transfer terminal diagnostics.
- [x] Add a JSON inventory tool for exact instruction words, addresses, and
  family counts.
- [x] Add fixture tests for COP1/COP2/COP3 and reserved-opcode classification.

### Remaining

- [x] Replace separate Python and C++ opcode-name tables with one generated or
  shared canonical opcode registry.
- [x] Add exact MIPS function names and operand decoding for every reserved
  SPECIAL function.
- [ ] Add exact disassembly for every inventory entry.
- [ ] Add source-fix stubs for instruction-family implementation functions.
- [ ] Track newly resolved instruction families and remove their unsupported
  stubs automatically.
- [ ] Ensure inventory counts exclude embedded data and unreachable code.

## Validation and quality

### Implemented

- [x] Add checked-in JSON executable fixtures for arithmetic, loads/stores,
  branches, calls, delay slots, entrypoints, memory, host integration, and
  unsupported paths.
- [x] Add malformed and truncated PS-X header fixtures.
- [x] Add end-to-end Linux ELF launch fixtures and terminal-output checks.
- [x] Add PE32+, SPIR-V, SDL input, host integration, and unsupported registry
  tests.
- [x] Add an opt-in generic reference-executable inventory/report tool.
- [x] Add CMake, CTest, layout, fixture, build, and format validation targets.

### Remaining

- [ ] Relink available reference executables and record first reachable
  unsupported gaps in build artifacts, never in title-specific test code.
- [ ] Add minimal copyright-safe fixtures for every new real-executable blocker.
- [ ] Run sanitizer builds and fuzz malformed PS-X headers/payloads.
- [ ] Add deterministic assembler/linker failure tests.
- [ ] Add ELF and PE reproducibility tests.
- [ ] Add cross-platform Linux/Windows build matrices.
- [ ] Add static analysis, compiler-warning cleanup, and memory/error audit.
- [ ] Add benchmark and timeout tests for CFG discovery and assembly generation.
- [ ] Add CMake release artifacts and installation/packaging rules.
- [ ] Keep TODO current after every converter, lowering, host, and format change.
