# TODO

## Relinker

- [ ] Validate native Windows PE layout output beyond header construction.
- [ ] Translate or lower MIPS instructions into native Linux/Windows code.
- [ ] Add checked-in, minimal PS-X executable fixtures for more executable layouts and malformed cases.
- [ ] Replace layout-only relinking behavior with an explicit native executable conversion state.

## CPU

- [ ] Complete COP0 exception cause/branch-delay EPC behavior for all exception classes.
- [ ] Implement remaining load-delay cancellation edge cases and cache isolation rules.
- [ ] Complete unaligned load/store behavior and memory-mapping boundaries.
- [ ] Implement all GTE instructions with fixed-point scaling, rounding, saturation, flags, and cycle accounting.
- [ ] Verify arithmetic overflow for ADD, ADDI, SUB, and signed divide edge cases.

## GPU

- [ ] Add textured polygon sampling, texture page/clut lookup, and bilinear-like PS1 behavior.
- [ ] Complete gouraud shading interpolation and semi-transparent blending.
- [ ] Implement drawing-area clipping, offsets, texture-window masking, and mask-bit behavior.
- [ ] Complete VRAM wrapping, interlaced display, 16/15/24-bit display modes, and GPU readback FIFO timing.
- [ ] Execute GPU linked-list DMA and GPU block/VRAM transfer behavior in real boot traces.

## DMA

- [ ] Implement DMA priority, bus arbitration, chaining completion, and DICR IRQ masking precisely.
- [ ] Add complete DMA channel 1 MDEC-in/MDEC-out, channel 3 CD-ROM, and channel 4 SPU scheduling.
- [ ] Remove silent no-op handling from implemented PS1 I/O ranges and report unsupported states explicitly.

## BIOS and Kernel

- [ ] Implement the remaining A0/B0/C0 function families, including file, event, thread, and card APIs.
- [ ] Model the event table layout and callbacks against both retail and OpenBIOS behavior.
- [ ] Load a real BIOS image explicitly without title-specific address shims.
- [ ] Verify exception vectors and kernel-memory behavior with BIOS-independent fixtures.

## CD-ROM

- [ ] Read sectors through a disc interface instead of relying on caller-supplied raw BIN assumptions.
- [ ] Implement complete CUE/BIN track, TOC, and seek/LBA conversion behavior.
- [ ] Add command timing, second responses, async IRQ deferral, Setmode/Sector size, ReadN/ReadS, Pause/Stop/Init, and motor state.
- [ ] Implement CD-XA sector parsing and 4-bit/8-bit XA-ADPCM playback.
- [ ] Implement CD-ROM DMA data FIFOs and IRQ/DataReady/DataEnd behavior.

## SPU

- [ ] Implement ADSR attack/decay/sustain/release rates instead of fixed phase timing.
- [ ] Decode BRR ADPCM blocks and repeating sample addresses correctly.
- [ ] Add SPU voice interpolation, noise, interpolation tables, reverb, and RAM IRQ behavior.
- [ ] Implement CD-XA mixing and 44.1 kHz host resampling.
- [ ] Implement SPU DMA reads/writes, transfer modes, register FIFOs, and status bits.

## Controller

- [ ] Complete DualShock/analog polling, config mode, force feedback, and host joystick mapping.
- [ ] Implement memory-card serial protocol reads/writes and save storage.
- [ ] Add keyboard/controller input tests.

## Timers and Interrupts

- [ ] Implement timer sync gates, dot-clock/hblank/vblank sources, and timer IRQ modes.
- [ ] Complete I_STAT/I_MASK acknowledgement and rising-edge source behavior.
- [ ] Dispatch BIOS and COP0 interrupts in a single generic exception pipeline.

## Validation

- [ ] Add fixture-based runtime tests for MIPS instructions, load delay, COP0 exceptions, GTE commands, GP0/GP1, DMA, CD-ROM, SPU, timers, and controllers.
- [ ] Relink and run Tekken 3, Castlevania, and both MGS boots after every runtime subsystem change.
- [ ] Record exact unsupported behavior in tests instead of allowing silent MMIO defaults.
- [ ] Remove remaining title-specific or virtualized memory behavior from the runtime.
