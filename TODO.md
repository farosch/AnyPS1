# TODO

## Relinker

- [ ] Validate native Windows PE layout output beyond header construction.
- [ ] Translate or lower MIPS instructions into native Linux/Windows code.
- [ ] Add checked-in, minimal PS-X executable fixtures for more executable layouts and malformed cases.
- [ ] Replace layout-only relinking behavior with an explicit native executable conversion state.

## CPU

- [x] Complete COP0 exception EPC/BD/CAUSE handling for syscalls, breaks, interrupts, alignment, and memory faults.
- [x] Implement remaining load-delay cancellation edge cases.
- [x] Complete cache isolation rules for KSEG0/KSEG1 RAM mapping.
- [x] Complete unaligned load/store behavior and PS1 RAM/MMIO boundaries.
- [x] Implement remaining GTE instructions with fixed-point scaling, rounding, saturation, flags, and cycle accounting.
- [x] Verify arithmetic overflow for ADD, ADDI, SUB, and signed divide edge cases.

## GPU

- [x] Add indexed and 15-bit textured polygon sampling with texture page/CLUT lookup; bilinear-like PS1 filtering remains a host-optimization gap.
- [x] Complete gouraud shading interpolation and PS1 semi-transparent blending/mask writes.
- [x] Implement drawing-area clipping, offsets, texture-window masking, and mask-bit behavior.
- [x] Implement GPU block/linked-list DMA dispatch and GP0 reset/readback/VRAM transfer hooks.
- [ ] Complete VRAM wrapping, interlaced display, 16/15/24-bit display modes, and GPU readback FIFO timing.
- [ ] Verify GPU linked-list DMA and GPU block/VRAM transfer behavior in real boot traces; no GP0/GP1 or DMA channel writes were observed in four available 300M-instruction runs.
- [x] Verify block-DMA-to-GP0 behavior with a fixture: MADR/BCR/CHCR reaches GP0 packets.
- [ ] All FFVII discs show DMA2 CHCR=0x401 and GP0/GP1 startup writes; linked-list payload and remaining real-frame validation require implementation.

## DMA

- [ ] Implement DMA priority, bus arbitration, and chaining completion precisely.
- [x] Implement DICR completion IRQ masking and GPU/OTC/CD-ROM/SPU transfer hooks.
- [x] Implement block-DMA and linked-list DMA dispatch for DMA channel 2.
- [ ] Add complete DMA channel 1 MDEC-in/MDEC-out scheduling.
- [x] Remove silent no-op handling from implemented PS1 I/O ranges and report unsupported states explicitly.

## BIOS and Kernel

- [ ] Implement the remaining A0/B0/C0 function families, including file, event, thread, and card APIs.
- [x] Model the event table layout and callback state against OpenBIOS behavior.
- [x] Complete BIOS-vector HLE event/VBlank state without external BIOS images.
- [x] Verified local BIOS binaries are optional behavioral references; runtime does not load or require them.
- [ ] Verify exception vectors and kernel-memory behavior with BIOS-independent fixtures.
- [ ] Complete generic VBlank/kernel timing globals and HLE WaitEvent state for boot executables.
- [ ] Reach DMA/GP0 submission in Tekken startup; C0/B0 VBlank counters advance but no GPU packets were observed in 300M instructions.

## CD-ROM

- [ ] Read sectors through a disc interface instead of relying on caller-supplied raw BIN assumptions.
- [x] Implement explicit and inferred Track 1 BIN sector reads for boot/runtime startup.
- [ ] Implement complete CUE/BIN track, TOC, and seek/LBA conversion behavior.
- [ ] Add command timing, second responses, async IRQ deferral, Setmode/Sector size, ReadN/ReadS, Pause/Stop/Init, and motor state.
- [ ] Implement CD-XA sector parsing and 4-bit/8-bit XA-ADPCM playback.
- [ ] Implement CD-ROM DMA data FIFOs and IRQ/DataReady/DataEnd behavior.

## SPU

- [ ] Implement ADSR attack/decay/sustain/release rates instead of fixed phase timing.
- [x] Decode basic BRR ADPCM blocks and repeating sample addresses.
- [ ] Add SPU voice interpolation, noise, interpolation tables, reverb, and RAM IRQ behavior.
- [ ] Implement CD-XA mixing and 44.1 kHz host resampling.
- [ ] Implement SPU DMA reads/writes, transfer modes, register FIFOs, and status bits.

## Controller

- [x] Complete DualShock/analog polling, config mode, force feedback, and host joystick mapping.
- [x] Implement memory-card serial protocol reads/writes and save storage.
- [x] Add keyboard/controller input tests.

## Timers and Interrupts

- [x] Implement timer sync gates, dot-clock/hblank/vblank sources, and timer IRQ modes.
- [x] Complete I_STAT/I_MASK acknowledgement and rising-edge source behavior.
- [x] Dispatch BIOS and COP0 interrupts in a single generic exception pipeline.

## Validation

- [x] Add fixture-based runtime tests for MIPS instructions and load-delay behavior.
- [x] Add fixture-based runtime tests for COP0 exceptions and the interrupt pipeline.
- [x] Add fixture-based runtime tests for GTE commands.
- [x] Add fixture-based GTE SQR and arithmetic-overflow coverage.
- [x] Add fixture-based GTE SQR, NCLIP, and AVSZ coverage.
- [x] Add fixture-based runtime tests for GP0/GP1 register and GPU command behavior.
- [x] Add fixture-based runtime tests for DMA channels and bus arbitration.
- [x] Add fixture-based runtime tests for CD-ROM commands, timing, and DMA.
- [x] Add fixture-based runtime tests for SPU voices, DMA, reverb, and sample playback.
- [x] Add fixture-based runtime tests for timers and controller transfers.
- [x] Add fixture-based runtime tests for memory-card reads and writes.
- [x] Relink and run Tekken 3, Castlevania, and both MGS boots after runtime subsystem changes.
- [x] Record exact unsupported behavior in tests instead of allowing silent MMIO defaults.
- [x] Remove remaining title-specific or virtualized memory behavior from the runtime.
