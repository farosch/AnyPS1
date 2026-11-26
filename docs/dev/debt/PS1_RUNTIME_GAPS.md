# PS1 Runtime Gaps

The native runtime is an initial interpreter, not a complete PlayStation 1 emulator.

## GPU

- Basic GP0 rectangles and triangles, GP1 display setup, VRAM 16-bit display, and
  CPU-to-VRAM transfer are implemented.
- Textured polygons, gouraud shading, drawing offsets, texture windows, VRAM-to-VRAM
  and VRAM-to-CPU transfers, and GPU DMA linked-list rendering require implementation.

## DMA

- Channel 2 block and linked-list transfer, channel 3 status, and channel 6 OTC
  clearing have initial support.
- Interrupt chaining, bus arbitration, memory-card DMA, MDEC DMA, and complete DMA
  priority behavior are not implemented.

## Audio

- An SDL audio device, SPU register ranges, DMA status, 24-voice key state, and stereo
  sample mixing are present.
- ADSR timing, BRR/XA-ADPCM decoding, reverb, noise, voice sampling, CD-XA mixing,
  and complete DMA-transfer scheduling require implementation.

## Controller

- A digital-pad byte sequence and keyboard mapping are implemented.
- Analog pads, DualShock configuration, memory-card serial protocol, vibration,
  and host joystick integration are not implemented.

## CD-ROM

- CD-ROM register FIFOs, command dispatch, response FIFOs, interrupt status, and
  basic Getstat/seek/read/status commands are present.
- Disc-sector reads, command timing, seek/media state, TOC, XA sectors, audio mixing,
  DMA data transfer, and second-response scheduling require implementation.

## CPU

- Core MIPS I instructions, load and branch-delay state, basic COP0 exceptions/RFE,
  BIOS event APIs, and practical GTE register/command handling are present.
- COP0 software interrupt details, cache semantics, memory-alignment edge cases,
  complete GTE overflow/rounding/saturation, and full GTE command coverage require
  implementation.

## Current Cross-Game Startup Blockers

- Tekken 3, Castlevania, and both Metal Gear Solid discs relink successfully, but
  100-million-instruction traces end at instruction limits without GP0/GP1 writes.
- Tekken 3 repeatedly executes its direct VBlank/frame-counter wait routine.
- Castlevania repeatedly waits through direct frame-counter and event wait paths.
- Both MGS discs repeatedly execute a main-loop callback/initialization routine.
- DMA channel writes and GPU linked-list/block submissions are not yet proven to
  reach GPU packets from the boot executables.
- BIOS/CD-ROM/SPU initialization and complete timing semantics remain the primary
  generic blocker surface, rather than relinker header/layout failure.
