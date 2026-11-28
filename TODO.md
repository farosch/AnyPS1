# TODO

## MIPS and COP lowering

- [x] Split native relinking dispatch into platform-specific `MipsLinuxLowerer`
  and `MipsWindowsLowerer` classes behind a shared `MipsLowerer` interface.
- [x] Replace fixed-block lowering with CFG lowering for fixture-reachable
  code and host/MMIO families, preserving explicit unsupported exits.
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
- [x] Resolve statically provable JALR targets and preserve unresolved
  indirect-control-flow exits and call-edge diagnostics.
- [x] Add branch-delay-slot emission and load-delay behavior.
- [x] Implement signed ADD/ADDI/SUB overflow exits.
- [x] Implement divide-by-zero exits.
- [x] Preserve load address, entrypoint, GP, SP, RAM, and scratchpad state.
- [x] Lower checked PS1 COP0 reads/writes where they correspond to host state.
- [x] Add explicit COP0/COP1/COP2/COP3 and coprocessor load/store stubs.
- [x] Name known GTE/COP2 operations and add exact GTE test coverage.
- [x] Lower named GTE operations NCLIP, OP, DPCS, INTPL, MVMVA, SQR, and RTV0
  directly in native Linux lowering and align exact inventory classification.
- [x] Classify exact reserved opcodes and SPECIAL functions.
- [x] Inventory unsupported instruction words and families across executable
  payloads.
- [x] Add native COP1 MFC1/MTC1/CFC1/CTC1, add/sub/mul/div/sqrt, Cvt.S.W,
  Cvt.W.S, C.EQ/LT/LE, BC1 branches, and checked LWC1/SWC1 memory paths.
- [x] Add remaining COP1 conversions/comparison variants and exhaustive
  floating-point edge-path fixtures.
- [x] Classify COP2/GTE instructions and named GTE operations with exact
  unsupported stubs; native matrix/vector/color lowering remains open.
- [x] Implement COP3 opcode classification and exact unsupported execution
  stubs.
- [x] Lower COP3 register transfers and COP0/COP3 load/store forms directly
  where the native memory mapping is implemented.
- [x] Implement the native COP1 LWC1/SWC1 subset and add checked unsupported
  stubs for LWC0-LWC3/SWC0-SWC3 coprocessor families.
- [x] Complete load-delay behavior for every MIPS load form and verify the
  delay-slot fixtures against real instruction sequences.
- [x] Verify exact MIPS signed/unsigned overflow, division edge cases,
  endianness, unaligned access, and HI/LO semantics with focused fixtures.
- [x] Add support for sparse control-flow regions and discontinuous executable
  sections in CFG discovery.
- [x] Preserve post-`JALR` continuation leaders and dispatch runtime
  indirect calls through lowered native leaders.
- [x] Recover conservative callback targets from contiguous executable
  pointer tables without seeding isolated data words.
- [x] Resolve zero-based immediate and payload-loaded indirect targets while
  invalidating stale constants on intervening register writes.
- [ ] Complete PS1 COP2/GTE matrix, vector, color, perspective, interpolation,
  saturation, and flag semantics beyond the shared runtime operation subset
  currently reached by reference executables.
- [ ] Extend indirect jump-table reconstruction to distinguish executable
  destinations from embedded data reached by runtime control flow.
- [ ] Add semantic fixtures for every COP2/GTE operation reached by real
  executable inventories.

## Linux executable conversion

- [x] Route Linux relinking exclusively through `MipsLinuxLowerer::lower`;
  remove legacy fixed-block and assembly lowering paths.
- [x] Add an explicit `--target linux|windows` parameter.
- [x] Generate ELF64 x86-64 executables with a native `_start` entrypoint.
- [x] Emit valid ELF entry, program-header, section-header, text, rodata,
  and BSS metadata.
- [x] Link Linux generated executables with Clang + `ld.lld`, including host SDL3/C++ runtime
  dependencies required by generated executable host-loop calls.
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
- [x] Eliminate shell-string linker invocation and use a repository-owned
  argument-safe toolchain API.
- [x] Separate Linux ELF linking and ELF64 validation into `ElfWriter` under
  `src/executable/`, mirroring `PeWriter`.
- [x] Remove all legacy assembly lowering and fixed-block code duplication.
- [x] Gate ELF32, ARM64, and other architecture targets behind native lowering
  backends; unsupported architectures fail explicitly instead of relinking.
- [x] Keep PIE/ASLR disabled because fixed, preserved PS1 addresses require an
  ET_EXEC-compatible layout.
- [x] Verify generated ELF program-header permissions and file offsets for
  text, rodata, bss, payload, and symbol tables.
- [x] Verify generated ELF artifacts with `readelf`, `file`, a linker validator,
  and direct launch in every supported Linux build environment.
- [x] Emit host-native sections only; reject or exclude PS1 payload bytes from
  executable state that are not referenced by lowered native code.

## Memory and address lowering

- [x] Route PS1 RAM-adjacent MMIO loads/stores through the native
  `mmio_state` mapping with explicit out-of-range faults.
- [x] Map 2 MiB PS1 RAM and 1 KiB scratchpad into native memory.
- [x] Normalize 32-bit PS1 addresses to host RAM/scratchpad offsets.
- [x] Preserve 8/16/32-bit load/store behavior and signed/unsigned extensions.
- [x] Emit checked bounds for RAM and scratchpad accesses.
- [x] Route out-of-range and MMIO accesses to explicit fault/unsupported exits.
- [x] Preserve executable payload bytes for native payload copying.
- [x] Copy executable payloads at their physical PS1 RAM load offset and reject
  payloads that exceed the mirrored 8 MiB RAM address window.
- [x] Map PS1 RAM mirror accesses throughout the 8 MiB mirror window to the
  backing 2 MiB host RAM.
- [x] Remove title-specific, BIOS-dependent, and virtualized-memory behavior.
- [x] Implement page/range validation for executable sections and RAM boundaries.
- [x] Add executable-section protection and separate code/data/rodata mappings.
- [x] Add a generic MMIO map with documented native implementations.
- [x] Lower PS1 DMA to native host memory copies or explicit unsupported exits.
- [x] Define generic CD-ROM, disc, cache, timers, controllers, memory cards,
  SPU, GPU, interrupt, and peripheral state mappings without title-specific behavior.
- [x] Implement 32-bit address wraparound consistently across every load,
  store, branch, jump, and call calculation.
- [x] Add exhaustive unaligned load/store and endianness fixtures.
- [x] Reject BIOS-dependent executable transitions explicitly.

## Host integration

- [x] Replace BIOS-vector assumptions with native host services or explicit
  unsupported results.
- [x] Implement the A0 `0x39` InitHeap BIOS service with native RAM bounds and
  return-address handling on Linux and Windows.
- [x] Add native Linux console write and process exit handling.
- [x] Implement generic file, input, audio, video, and Vulkan host mappings.
- [x] Add SDL3 gamepad, keyboard, and mouse runtime support.
- [x] Add analog sticks, triggers, shoulders, D-pad, and face buttons.
- [x] Add `anyps1-input.ini` parsing and default bindings.
- [x] Add host mapping and input integration fixtures.
- [x] Add unsupported MMIO and BIOS-vector fixtures.
- [x] Implement native file lowering for open/read/write/seek/close operations
  in Linux/Windows lowering at PS1 A0 file-vector calls.
- [x] Integrate input polling into the actual generated executable loop.
- [x] Integrate SDL3 window creation and event pumping into the Linux/Windows
  generated executable loop.
- [x] Integrate native audio lowering for PS1 SPU operations into generated
  executable loops.
- [x] Integrate native video framebuffer presentation into generated executable
  loops.
- [x] Integrate native Vulkan swapchain, image, sampler, pipeline, and
  command-buffer resources.
- [x] Expose host-native diagnostics through generated executable structured
  exit codes and logs.
- [x] Integrate title-independent memory-card storage mapping into generated
  executable loops.
- [x] Integrate title-independent CD/disc filesystem and asset access mapping
  into generated executable loops.
- [x] Add explicit unsupported exits for every unimplemented BIOS vector and
  MMIO device family.
- [x] Implement generated-loop timer, interrupt, DMA, controller, CD-ROM,
  GPU, and SPU state progression.
- [x] Verify generated executables progress through real host frames with
  VBlank scheduling, event/input polling, audio, and framebuffer presentation.
- [x] Integrate memory-card and disc runtime APIs through the actual PS1
  controller and CD-ROM/MMIO protocols.
- [x] Implement the remaining B0/C0 BIOS services reached by real executable
  startup paths beyond A0 byte copy.
- [x] Advance real-executable conversion past the first unsupported COP2/MMIO
  family and implement DMA linked-list transfers plus generic CD-ROM sector
  progression. The next concrete runtime blocker is real Windows MIPS native
  instruction emission rather than a diagnostic-only PE artifact.

## Windows executable conversion

- [x] Generate PE32+ layout for AMD64 with DOS and PE signatures.
- [x] Add COFF headers, optional headers, text/rdata sections, entrypoint,
  imports, relocations, architecture fields, and host diagnostics.
- [x] Add PE signature, machine, section, optional-header, and import-directory
  fixture validation.
- [x] Add `objdump`/`llvm-readobj` format-inspector support.
- [x] Add a shared semantic parity fixture that executes on Linux and Windows.
- [x] Use explicit Windows lowering diagnostics rather than silent success.
- [x] Replace the Windows manual PE layout and MASM/link.exe path with the
  shared native lowering IR and Clang/LLD backend.
- [x] Implement initial real Windows native MIPS instruction emission for the
  shared lowering IR, with RAM, scratchpad, arithmetic, branches, loads/stores,
  HI/LO, and explicit unsupported exits.
- [x] Replace hard target-source size rejection with shared sparse leader
  dispatch for Linux and Windows; bounded assembler/Clang jobs remain explicit
  and large executable behavior is verified across multiple reference games.
- [ ] Implement chunked native lowering sources instead of one sparse
  translation unit: split Linux assembly and Windows C by leader bucket,
  assemble/link every chunk, and share runtime state across chunk boundaries.
- [x] Apply the 30,000-leader CFG emission limit and a two-minute
  assembler/linker timeout explicitly to Linux lowering; Linux no longer hangs
  on oversized assembly or a stalled native toolchain.
- [x] Generate imports and relocations for real Windows host-native code.
- [x] Document PE32 support as optional: no PE32-only x86 compatibility target
  is required because the native backend emits AMD64 PE32+.
- [x] Test generated PE32+ output with available Windows cross-toolchains.
- [x] Add Windows execution fixtures and exit-status/diagnostic validation.
- [x] Integrate Windows SDL3 input and Vulkan host services into the generated
  executable loop.
- [x] Validate generated PE images with platform format inspectors and signed
  executable tooling as applicable.

## PS1 GPU lowering and Vulkan shaders

- [x] Define a generic GP0/GP1 and VRAM graphics IR.
- [x] Lower GP0/GP1 state changes into Vulkan resource plans.
- [x] Emit direct SPIR-V shader modules for polygon, textured-polygon, sprite,
  rectangle, and framebuffer primitive categories.
- [x] Add generic SPIR-V validation and optimization test support.
- [x] Add Vulkan/Linux and Vulkan/Windows platform enums without CUDA/NVIDIA
  dependencies.
- [x] Preserve unsupported GPU commands as explicit conversion failures.
- [x] Model GP0/GP1 command payloads completely, including VRAM transfers,
  draw areas, texture windows, display areas, display modes, DMA direction,
  and display enable state.
- [x] Emit primitive-specific SPIR-V shaders rather than a shared minimal module.
- [x] Implement PS1 blending modes, semi-transparency, dithering, 16/24-bit
  color, CLUT/texture lookup, texture-page modes, and framebuffer reads.
- [x] Implement polygon, textured-polygon, sprite, rectangle, line, and
  framebuffer operations with native Vulkan pipelines.
- [x] Implement VRAM-to-host-image and host-image-to-VRAM transfers.
- [x] Create and validate Vulkan resources in the Linux/Windows runtime.
- [x] Integrate shader validation and optimization into release builds.
- [x] Add SPIR-V golden fixtures and malformed GPU command fixtures.
- [x] Add a cross-target parity fixture covering common Linux/Windows MIPS
  opcode sets and GPU primitive/resource-plan mappings.
- [x] Preserve unsupported GPU commands as conversion failures, including no
  silent drop or emulator fallback.

## Unsupported instruction tracking

- [x] Distinguish executable code from embedded data, tables, padding,
  `.rodata`, and payload regions during unsupported-instruction counts.
- [x] Report supported, reachable, unsupported, leader, call-edge,
  unresolved-indirect, and first-gap metrics without counting data as code.
- [x] Remove directly lowerable reserved-opcode and coprocessor stubs while
  retaining explicit failures for irreducibly unsupported operations.
- [x] Replace substring-based unsupported-instruction heuristics with the
  shared lowering, disassembly, diagnostic, and inventory classifier.
- [x] Add a C++ unsupported-instruction classifier for SPECIAL, COP0/COP1/COP2/
  COP3, coprocessor load/store, and reserved opcodes.
- [x] Add exact GTE/COP2 function names for known operations.
- [x] Emit category-specific native unsupported stubs.
- [x] Add first-gap, unsupported-count, coverage, call-edge, and unresolved
  transfer terminal diagnostics.
- [x] Add a JSON inventory tool for exact instruction words, addresses, and
  family counts.
- [x] Add fixture tests for COP1/COP2/COP3 and reserved-opcode classification.
- [x] Replace separate Python and C++ opcode-name tables with one generated or
  shared canonical opcode registry.
- [x] Add exact MIPS function names and operand decoding for every reserved
  SPECIAL function.
- [x] Add exact disassembly for every inventory entry.
- [x] Add source-fix stubs for instruction-family implementation functions.
- [x] Track newly resolved instruction families and remove their unsupported
  stubs automatically.
- [x] Ensure inventory counts exclude embedded data and unreachable code.

## Validation and quality

- [x] Add per-instruction fixtures for MIPS I, COP0, COP1, COP2/GTE, COP3,
  coprocessor load/store, and reserved-opcode behavior.
- [x] Add checked-in JSON executable fixtures for arithmetic, loads/stores,
  branches, calls, delay slots, entrypoints, memory, host integration, and
  unsupported paths.
- [x] Add malformed and truncated PS-X header fixtures.
- [x] Add end-to-end Linux ELF launch fixtures and terminal-output checks.
- [x] Add PE32+, SPIR-V, SDL input, host integration, and unsupported registry
  tests.
- [x] Add an opt-in generic reference-executable inventory/report tool.
- [x] Add CMake, CTest, layout, fixture, build, and format validation targets.
- [x] Relink available reference executables and record first reachable
  unsupported gaps in build artifacts, never in title-specific test code.
- [x] Add minimal copyright-safe fixtures for every new real-executable blocker.
- [x] Add malformed PS-X header fuzz coverage and AddressSanitizer CI.
- [x] Add payload mutation fuzz coverage for malformed PS-X headers and payload
  sizes; retain the deterministic corpus in CTest fixtures.
- [x] Add deterministic linker/output-failure tests and CFG timeout tests.
- [x] Add ELF and PE reproducibility tests.
- [x] Add cross-platform Linux/Windows build matrices.
- [x] Add strict compiler-warning policy and CMake static build targets.
- [x] Run strict compiler-warning builds, AddressSanitizer tests, cppcheck,
  and clang-tidy in the CI static-analysis workflow.
- [x] Add benchmark and timeout tests for CFG discovery and assembly generation.
- [x] Add CMake release artifacts and installation/packaging rules.
- [x] Keep TODO current after every converter, lowering, host, and format change.
