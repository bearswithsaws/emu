# NES Emulator Technical Documentation

## Table of Contents
1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Memory Maps](#memory-maps)
4. [Component Details](#component-details)
5. [Build Instructions](#build-instructions)
6. [ROM Format](#rom-format)
7. [Current Implementation Status](#current-implementation-status)
8. [Known Issues](#known-issues)
9. [Audio/Video Sync Strategy](#audiovideo-sync-strategy)
10. [Future Work](#future-work)

---

## Overview

This is a Nintendo Entertainment System (NES) emulator written in C using SDL2 for graphics. The emulator aims to accurately replicate the NES hardware including:

- **MOS 6502 CPU** (actually a 2A03 variant without decimal mode)
- **Ricoh 2C02 PPU** (Picture Processing Unit)
- **Cartridge system** with multiple mapper support
- **SDL2-based display system** using modular display library (lib/display/)

### Design Goals
- Cycle-accurate CPU emulation
- Scanline-accurate PPU rendering
- Support for common mappers (starting with Mapper 0, 1, 2, 3)
- Cross-platform support (Linux and Windows)
- Maintainable, well-documented code

---

## Architecture

### High-Level Component Diagram

```
┌─────────────────────────────────────────────────────────┐
│              SDL2 Display Library Layer                 │
│  (Window, Rendering - lib/display/display.c/h)         │
│  Generic, reusable display abstraction                 │
└────────────┬────────────────────────────────────────────┘
             │
             │ Frame buffer, Input events
             │
┌────────────▼────────────────────────────────────────────┐
│                  NES Bus (nesbus.c/h)                   │
│  Memory-mapped I/O coordinator                          │
│  Routes reads/writes to appropriate components          │
└─────┬────────────┬────────────┬────────────────────┬────┘
      │            │            │                    │
      │            │            │                    │
┌─────▼─────┐ ┌───▼────┐ ┌─────▼──────┐  ┌─────────▼──────┐
│  6502 CPU │ │ 2C02   │ │ Cartridge  │  │  Controller    │
│           │ │  PPU   │ │  + Mapper  │  │  Input         │
│ (6502.c)  │ │(2c02.c)│ │(cartridge.c│  │  (controller.c │
│           │ │        │ │ mapper_*.c)│  │   nes_input.c) │
└───────────┘ └────────┘ └────────────┘  └────────────────┘
```

### Module Interaction

**CPU ↔ Bus:**
- CPU reads/writes through bus interface
- Bus routes to PPU registers ($2000-$2007), controller ($4016-$4017), or cartridge

**PPU ↔ Bus:**
- PPU reads pattern/nametable data through cartridge mapper
- PPU writes to CPU via NMI interrupt line (vblank)
- Frame buffer written to GUI for display

**Cartridge ↔ Mapper:**
- Cartridge loads ROM and instantiates appropriate mapper
- Mapper handles bank switching and special hardware

**GUI ↔ Emulation:**
- GUI event loop drives emulation timing
- CPU/PPU tick in sync (3 PPU cycles per CPU cycle)
- Frame completion triggers SDL_RenderPresent

---

## Memory Maps

### CPU Memory Map ($0000 - $FFFF)

| Address Range   | Size  | Description                                    |
|-----------------|-------|------------------------------------------------|
| $0000 - $07FF   | 2KB   | Internal RAM (mirrored 4x to $1FFF)           |
| $0800 - $1FFF   | -     | Mirrors of $0000-$07FF                        |
| $2000 - $2007   | 8B    | PPU Registers (mirrored to $3FFF)             |
| $2008 - $3FFF   | -     | Mirrors of $2000-$2007                        |
| $4000 - $4017   | 24B   | APU and I/O Registers                         |
| $4018 - $401F   | 8B    | APU and I/O (usually disabled)                |
| $4020 - $FFFF   | ~49KB | Cartridge space (PRG-ROM, PRG-RAM, mappers)   |

**Important Vectors:**
- $FFFA-$FFFB: NMI vector (PPU vblank interrupt)
- $FFFC-$FFFD: Reset vector (power-on/reset)
- $FFFE-$FFFF: IRQ/BRK vector (maskable interrupt)

### PPU Memory Map ($0000 - $3FFF)

| Address Range   | Size  | Description                                    |
|-----------------|-------|------------------------------------------------|
| $0000 - $0FFF   | 4KB   | Pattern Table 0 (tiles)                       |
| $1000 - $1FFF   | 4KB   | Pattern Table 1 (tiles)                       |
| $2000 - $23FF   | 1KB   | Nametable 0                                   |
| $2400 - $27FF   | 1KB   | Nametable 1                                   |
| $2800 - $2BFF   | 1KB   | Nametable 2                                   |
| $2C00 - $2FFF   | 1KB   | Nametable 3                                   |
| $3000 - $3EFF   | -     | Mirrors of $2000-$2EFF                        |
| $3F00 - $3F1F   | 32B   | Palette RAM (bg/sprite palettes)              |
| $3F20 - $3FFF   | -     | Mirrors of $3F00-$3F1F                        |

**Note:** Nametable mirroring depends on cartridge configuration (horizontal, vertical, single-screen, or 4-screen).

### PPU Registers (CPU $2000-$2007)

| Address | Name      | Description                                        |
|---------|-----------|---------------------------------------------------|
| $2000   | PPUCTRL   | Control register (NMI enable, sprite size, etc.)  |
| $2001   | PPUMASK   | Mask register (enable rendering, color effects)   |
| $2002   | PPUSTATUS | Status register (vblank, sprite 0 hit, overflow)  |
| $2003   | OAMADDR   | OAM address for $2004                             |
| $2004   | OAMDATA   | OAM data read/write                               |
| $2005   | PPUSCROLL | Scroll position (2 writes: X, Y)                  |
| $2006   | PPUADDR   | PPU address for $2007 (2 writes: high, low)       |
| $2007   | PPUDATA   | PPU data read/write                               |

---

## Component Details

### 6502 CPU ([arch/6502/6502.c](arch/6502/6502.c))

**Implementation Status:** ~95% complete

**Features:**
- All 56 legal opcodes implemented
- 13 addressing modes (Implied, Accumulator, Immediate, Zero Page, Zero Page X/Y, Relative, Absolute, Absolute X/Y, Indirect, Indexed Indirect, Indirect Indexed)
- Cycle-accurate timing with page boundary detection
- Stack operations (256-byte stack at $0100-$01FF)
- Status register: NV-BDIZC flags
- Interrupt support (NMI fully functional, IRQ implemented)

**Instruction Dispatch:**
The CPU uses a 3D lookup table `instructions[c][a][b]` based on opcode bit pattern:
```
Opcode: aaabbbcc
- cc (bits 0-1): Instruction group
- bbb (bits 2-4): Addressing mode/variant
- aaa (bits 5-7): Operation type
```

**Known Limitations:**
- BRK instruction incomplete (exits program)
- Decimal mode not implemented (intentional - NES uses 2A03)
- Invalid opcodes cause exit() unless `INVALID_AS_NOP` defined

**Cycle Timing:**
- Base cycle count per instruction
- +1 cycle for page boundary crossing (indexed addressing)
- +1 cycle for branch taken, +2 if branch crosses page

### 2C02 PPU ([arch/6502/2c02.c](arch/6502/2c02.c))

**Implementation Status:** ~85% complete (major progress 2025-11-12)

**Fully Implemented:**
- ✅ All 8 PPU register read/write handlers (PPUCTRL, PPUMASK, PPUSTATUS, OAMADDR, OAMDATA, PPUSCROLL, PPUADDR, PPUDATA)
- ✅ PPUADDR two-write latch mechanism (w register)
- ✅ PPUDATA auto-increment with read buffer (buffered reads $0000-$3EFF, immediate palette reads)
- ✅ Complete loopy scroll registers (v, t, x, w)
- ✅ Full background rendering pipeline with tile fetch (NT, AT, PT low/high)
- ✅ 16-bit shift registers for pattern data (pattern_shift_lo/hi)
- ✅ 8-bit shift registers for attribute data (attribute_shift_lo/hi)
- ✅ Scanline timing (262 scanlines per frame, 341 dots per scanline)
- ✅ VBlank timing (scanline 241, dot 1 - fixed 2025-11-12)
- ✅ NMI generation on VBlank
- ✅ Horizontal scrolling (coarse X increment, fine X)
- ✅ Vertical scrolling (coarse Y increment, fine Y)
- ✅ Nametable mirroring (horizontal, vertical, single-screen)
- ✅ Palette system (background and sprite palettes)
- ✅ Sprite evaluation (secondary OAM)
- ✅ Basic sprite rendering

**Partially Implemented:**
- ⚠️ Sprite rendering (85% - missing horizontal/vertical flip attributes)
- ⚠️ Sprite 0 hit detection (needs testing)
- ⚠️ 8x16 sprite mode (needs edge case handling)

**NOT Yet Implemented:**
- ❌ Open bus behavior (returns last value on bus)
- ❌ Sprite overflow flag accuracy
- ❌ Some PPU read/write side effects edge cases

**PPU Timing:**
- 341 PPU cycles per scanline
- 262 scanlines per frame (NTSC)
- Scanlines 0-239: Visible rendering
- Scanline 240: Post-render (idle)
- Scanlines 241-260: Vblank
- Scanline 261: Pre-render
- 60.0988 Hz frame rate (NTSC)

### Cartridge System ([arch/6502/cartridge.c](arch/6502/cartridge.c))

**Implementation Status:** ~90% complete

**Features:**
- iNES 1.0 format parsing
- iNES 2.0 detection (not fully supported)
- PRG-ROM and CHR-ROM extraction
- Trainer support (512-byte region)
- Mapper ID detection
- Mirroring mode detection
- Memory-mapped file loading (mmap)

**iNES Header Format (16 bytes):**
```
Offset  Size  Description
------  ----  -----------
0       4     Magic: "NES" + $1A
4       1     PRG-ROM size (16KB units)
5       1     CHR-ROM size (8KB units, 0 = CHR-RAM)
6       1     Flags 6: Mirroring, battery, trainer, mapper low nibble
7       1     Flags 7: VS System, PlayChoice-10, NES 2.0, mapper high nibble
8       1     PRG-RAM size (8KB units, rare)
9       1     TV system (NTSC/PAL)
10      6     Unused (should be zero)
```

### Mapper System

#### Mapper 000 - NROM ([arch/6502/mapper_000.c](arch/6502/mapper_000.c))

**Status:** ✅ Complete

**Specifications:**
- PRG-ROM: 16KB or 32KB (no banking)
- CHR-ROM: 8KB (or CHR-RAM)
- 16KB PRG-ROM is mirrored to fill 32KB space
- Nametable mirroring: Horizontal or Vertical

**Compatible Games:** ~28 games including Donkey Kong, Balloon Fight, Excitebike

#### Mapper 001 - MMC1 ([arch/6502/mapper_001.c](arch/6502/mapper_001.c))

**Status:** 🚧 50% Implemented (241 lines, needs testing)

**Currently Implemented:**
- ✅ 5-write shift register with serial interface ($8000-$FFFF)
- ✅ Reset detection (consecutive writes)
- ✅ All 4 internal registers (control, CHR0, CHR1, PRG)
- ✅ All 3 PRG bank modes (32KB, fix first, fix last)
- ✅ CHR bank switching (4KB and 8KB modes)
- ✅ Programmable mirroring (H/V/single-screen)
- ✅ PRG-RAM enable/disable

**Not Yet Tested:**
- ❌ Real game compatibility verification
- ❌ Edge cases and timing
- ❌ PRG-RAM save functionality

**Specifications:**
- PRG-ROM: Up to 256KB (16KB or 32KB banking)
- CHR-ROM: Up to 128KB (4KB or 8KB banking)
- 5-write shift register ($8000-$FFFF)
- 4 internal registers (control, CHR0, CHR1, PRG)
- Programmable mirroring (H/V/single-screen)

**Compatible Games:** ~680 games including Metroid, Mega Man 2, Legend of Zelda, Castlevania II

#### Mapper 002 - UxROM ([arch/6502/mapper_002.c](arch/6502/mapper_002.c))

**Status:** ❌ Stub (future work)

**Specifications:**
- PRG-ROM: Up to 256KB (16KB switchable + 16KB fixed)
- CHR-RAM: 8KB (no CHR-ROM banking)
- Simple write to $8000-$FFFF selects bank

**Compatible Games:** ~270 games including Mega Man, Castlevania, Duck Tales

#### Mapper 003 - CNROM ([arch/6502/mapper_003.c](arch/6502/mapper_003.c))

**Status:** ❌ Stub (future work)

**Specifications:**
- PRG-ROM: 16KB or 32KB (no banking)
- CHR-ROM: Up to 32KB (8KB banking)
- Simple write to $8000-$FFFF selects CHR bank

**Compatible Games:** ~155 games including Arkanoid, Paperboy, Pipe Dream

### NES Bus ([arch/6502/nesbus.c](arch/6502/nesbus.c))

**Implementation Status:** ~85% complete

**Features:**
- 2KB internal RAM with mirroring
- PPU register routing ($2000-$3FFF)
- Controller I/O routing ($4016-$4017)
- Cartridge routing ($4020-$FFFF)
- APU register routing ($4000-$4017)

**Not Implemented:**
- DMA transfer ($4014)

---

## Build Instructions

### Prerequisites

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libsdl2-dev libsdl2-ttf-dev
```

**Windows (via MSYS2/MinGW):**
```bash
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-SDL2
pacman -S mingw-w64-x86_64-SDL2_ttf
```

### Building

```bash
# Create build directory
mkdir -p build
cd build

# Configure
cmake ..

# Build
cmake --build .

# The executable will be at build/emu
```

### Running

```bash
# Basic usage
./emu <path_to_rom.nes>

# Example
./emu ~/roms/mario.nes
```

### Build Options

Edit `CMakeLists.txt` or use `-D` flags:

```bash
# Enable debug logging
cmake -DDEBUG=ON ..

# Release build (future)
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### Code Formatting

```bash
# Format all source files
clang-format -i *.c *.h arch/6502/*.c arch/6502/*.h
```

---

## ROM Format

### iNES File Structure

```
[Header: 16 bytes]
[Trainer: 0 or 512 bytes] (if present in header flags)
[PRG-ROM: N × 16KB]
[CHR-ROM: N × 8KB] (if CHR size > 0)
[PlayChoice data: optional]
```

### Supported Features
- ✅ iNES 1.0 format
- ⚠️ iNES 2.0 detection (not fully parsed)
- ✅ Trainer support
- ✅ PRG-ROM up to 8MB
- ✅ CHR-ROM up to 8MB
- ❌ PlayChoice data (ignored)

### Finding ROMs

**Legal Options:**
- Create your own homebrew ROMs
- Test ROMs: https://github.com/christopherpow/nes-test-roms
- Public domain ROMs

**Important:** Do not distribute or download copyrighted ROMs without permission.

---

## Current Implementation Status

### ✅ Completed
- **6502 CPU** (~98% complete)
  - All 56 legal opcodes implemented
  - All 13 addressing modes working
  - Cycle-accurate timing with page boundary detection
  - NMI interrupt support working
  - IRQ support partial
- **Cartridge System** (~95% complete)
  - iNES 1.0 format parsing
  - PRG-ROM and CHR-ROM extraction
  - Trainer support
  - Mapper 000 (NROM) fully functional
- **Bus Architecture** (100% complete)
  - Memory-mapped I/O routing
  - PPU register interface
  - APU register routing ($4000-$4017)
  - Controller I/O ($4016/$4017)
- **NES Controller Input** (~80% complete)
  - Controller structure and shift register logic
  - Button mapping (Arrow keys, Z=A, X=B, Enter=Start, RShift=Select)
  - Read/write implementation
  - See: arch/6502/controller.c, arch/6502/nes_input.c
- **APU (2A03)** (✅ Complete — 2026-05-17)
  - ✅ Pulse 1 & 2: duty sequencer, envelope, sweep unit, length counter
  - ✅ Triangle: linear counter, length counter, 32-step sequencer
  - ✅ Noise: 15-bit LFSR (long/short modes), envelope, length counter
  - ✅ DMC: delta PCM playback, memory reader via bus, loop, IRQ
  - ✅ Frame counter: 4-step and 5-step modes, quarter/half-frame clocking, IRQ
  - ✅ Nonlinear hardware-accurate mixer
  - ✅ SDL2 audio queue output (44100 Hz, mono float32)
  - ✅ Frame IRQ and DMC IRQ wired into CPU IRQ mechanism
  - See: arch/6502/apu.c, arch/6502/apu.h

### 🚧 In Progress
- **PPU Background Rendering** (~85% complete) - **MAJOR PROGRESS 2025-11-12**
  - ✅ Complete PPU register interface ($2000-$2007)
  - ✅ Loopy scrolling registers (v, t, x, w)
  - ✅ Nametable mirroring (horizontal/vertical)
  - ✅ Background tile fetching (cycle-accurate 8-dot pattern)
  - ✅ Shift registers for pixel rendering
  - ✅ Pattern table reads from CHR-ROM
  - ✅ Attribute table palette selection
  - ✅ Palette RAM and backdrop color
  - ✅ VBlank timing and NMI triggering (scanline 241)
  - ✅ Pre-render scanline preparation (scanline -1)
  - ✅ Frame buffer output to SDL
  - ✅ **Three critical bugs fixed 2025-11-12** (see Known Issues)
  - ❌ Fine scrolling edge cases
  - ❌ Mid-scanline effects

- **PPU Sprite Rendering** (~40% complete)
  - ✅ OAM structure and secondary OAM
  - ✅ Sprite evaluation logic
  - ✅ 8x8 and 8x16 sprite modes
  - ✅ Horizontal/vertical flip
  - ✅ Basic sprite rendering pixel logic
  - ❌ Complete sprite 0 hit detection
  - ❌ Sprite overflow flag accuracy
  - ❌ Sprite rendering edge cases

- **GUI/Display Integration** (~90% complete)
  - ✅ SDL2 window creation and rendering (lib/display/)
  - ✅ Frame buffer display (256x240)
  - ✅ Event loop integrated with emulation
  - ✅ Frame timing (60 Hz via PPU frame_complete flag)
  - ✅ Pause/resume functionality
  - ✅ Window close handling
  - ⚠️ Old gui.c/gui.h removed, replaced with modular display library

- **Mapper 001 (MMC1)** (~50% complete)
  - ✅ Serial write interface (5-write shift register)
  - ✅ Register structure
  - ❌ PRG bank switching (16KB/32KB modes)
  - ❌ CHR bank switching (4KB/8KB modes)
  - ❌ Mirroring control

- **Frame Timing** (100% complete)
  - ✅ 60 Hz frame rate via PPU
  - ✅ VBlank synchronization
  - ✅ NMI triggering

### ❌ Not Started
- Save states (Phase 3)
- Debugging tools (Phase 4)
- Performance optimization (Phase 5)

### Code Quality Status
- ✅ .clang-format configuration
- ✅ Travis CI build automation
- ✅ **Comprehensive PPU documentation** (added 2025-11-12)
  - PPU_ARCHITECTURE_COMPLETE.md (51,000+ tokens)
  - PPU_REGISTERS_REFERENCE.md
  - PPU_RENDERING_PIPELINE.md
  - PPU_IMPLEMENTATION_COMPARISON.md
  - BUGFIXES_APPLIED.md
- ✅ Unit tests: PPU clock tests (8 tests), APU unit tests (8 tests, 72 assertions)
- ✅ Documentation (CLAUDE.md updated 2026-05-17)

---

## Known Issues

### Critical Bugs (Fixed)
- ~~Page boundary detection in ABX/ABY addressing~~ ✅ Fixed (pre-2025-11-12)
- ~~**PPUSTATUS VBlank hack**~~ ✅ **Fixed 2025-11-12** - Removed hardcoded vblank flag, frame timing now correct
- ~~**PPUDATA read buffer missing**~~ ✅ **Fixed 2025-11-12** - Implemented proper buffering for CHR/nametable reads
- ~~**Coarse X increment timing off by one cycle**~~ ✅ **Fixed 2025-11-12** - Eliminated 8-pixel viewport offset

### High Priority (Remaining)
1. **Sprite rendering incomplete** - Basic evaluation works, pixel rendering needs completion for visual correctness
2. **Sprite 0 hit detection incomplete** - Stub present, needs full implementation for split-screen effects
3. **Mapper 001 (MMC1) incomplete** - 50% done, needs bank switching logic to run 680+ games (Metroid, Zelda, Mega Man 2)

### Medium Priority
1. **Global static variables** - Only one emulator instance possible
2. **Aggressive exit() calls** - No graceful error handling (noted in user selection)
3. **Shift register timing** - Minor discrepancy (shifts on dots 1-256 instead of 2-257)
4. **Hardcoded paths** - ROM and font paths may be hardcoded in some areas

### Low Priority
1. **Debug printf spam** - Many debug prints not gated properly
2. **Commented-out code** - Old code left in comments
3. **Memory leaks** - Cartridge mmap never explicitly freed
4. **Circular header dependencies** - Fragile include structure (may be resolved with modular display lib)

---

## Audio/Video Sync Strategy

### Timing Fundamentals

**NES (NTSC) Timing:**
- CPU: 1.789773 MHz (1,789,773 Hz)
- PPU: 5.369318 MHz (3× CPU speed)
- Frame rate: 60.0988 Hz
- APU Frame Counter: 240 Hz (every 4th frame)

**Derived Timing:**
- CPU cycles per frame: 29,780.5 cycles
- PPU cycles per frame: 89,342 cycles (341 × 262)
- Scanlines per frame: 262
- PPU dots (cycles) per scanline: 341

### Audio Timing Strategy (Implemented 2026-05-17)

**Audio Sample Rate:** 44100 Hz
- Samples per frame: 44100 / 60.0988 ≈ 735 samples
- APU downsamples from 1.789773 MHz CPU clock using a counter accumulator

**Implemented Approach:**
1. **Per-CPU-cycle APU clocking** — `apu_clock()` called once per CPU cycle alongside `cpu->clock()` in the main loop
2. **Box-filter downsampling** — cycle counter accumulates; when >= cycles_per_sample, one sample emitted
3. **SDL2 queue mode** — samples accumulated in per-frame buffer (~735 samples), queued to SDL after each PPU frame
4. **Backpressure throttle** — if SDL queue > 2 frames, delay 1ms; this naturally limits emulation speed to ~60 Hz when audio is active

**APU-PPU Coupling:**
- APU frame counter triggers every ~3729/7457/11186/14915 CPU cycles
- Length counters, envelopes tick on frame counter quarter/half-frame events
- APU triangle clocks every CPU cycle; pulse/noise clock every other CPU cycle
- DMC IRQ and frame counter IRQ trigger CPU IRQ line

---

## Recent Work

### 2026-05-17 Session: Full APU Implementation
**All 5 NES APU channels implemented plus frame counter, mixer, and SDL2 audio output:**

- **New files:** `arch/6502/apu.c`, `arch/6502/apu.h`
- **Modified:** `arch/6502/nesbus.c/.h`, `arch/6502/CMakeLists.txt`, `emu.c`, `CMakeLists.txt`
- Pulse 1 & 2: duty sequencer (4 modes), envelope generator, sweep unit, length counter
- Triangle: 32-step sequencer, linear counter, length counter
- Noise: 15-bit LFSR with long/short modes, envelope, length counter
- DMC: delta-PCM playback, cpu_read callback, loop, IRQ
- Frame counter: 4-step and 5-step modes, IRQ generation
- Nonlinear mixer using hardware-accurate lookup formula
- SDL2 audio device opened in queue mode; samples flushed per PPU frame
- APU/DMC IRQ wired to CPU IRQ mechanism
- 8 new unit tests in `tests/test_apu.c` (72 assertions, all passing)

### 2025-11-12 Session: Critical PPU Bugfixes
**Three critical bugs fixed that were causing rendering offset and timing issues:**

1. **PPUSTATUS VBlank Hack Removed** (arch/6502/2c02.c:147)
   - Removed hardcoded `vblank_started = 1` line
   - Frame timing now works correctly
   - VBlank flag properly set at scanline 241, dot 1

2. **PPUDATA Read Buffer Implemented** (arch/6502/2c02.c:171-192, 2c02.h:142)
   - Added internal read buffer to PPU structure
   - Implemented proper buffering for CHR/nametable reads ($0000-$3EFF)
   - Palette reads ($3F00-$3FFF) bypass buffer correctly
   - Games can now read VRAM data properly

3. **Coarse X Increment Timing Fixed** (arch/6502/2c02.c:698-704)
   - Moved increment from case 0 (dots 9,17,25...) to case 7 (dots 8,16,24...)
   - **Eliminated 8-pixel viewport offset**
   - Shift registers now properly pre-loaded before rendering
   - Background graphics render at correct positions

**Comprehensive Documentation Created:**
- [PPU_ARCHITECTURE_COMPLETE.md](PPU_ARCHITECTURE_COMPLETE.md) (51,000+ tokens)
- [PPU_REGISTERS_REFERENCE.md](PPU_REGISTERS_REFERENCE.md)
- [PPU_RENDERING_PIPELINE.md](PPU_RENDERING_PIPELINE.md) (1,000+ lines)
- [PPU_IMPLEMENTATION_COMPARISON.md](PPU_IMPLEMENTATION_COMPARISON.md) - Spec vs implementation
- [BUGFIXES_APPLIED.md](BUGFIXES_APPLIED.md) - Complete record of fixes

**Status:** Mapper 0 (NROM) games should now be **visually playable** (sprites need completion)

---

## Future Work

### Phase 1: Core Functionality (~75% complete, up from ~30%)
- [X] ~~Implement PPU background rendering~~ ✅ **85% complete** (2025-11-12)
- [🚧] **Complete PPU sprite rendering** (40% complete) - **HIGHEST PRIORITY NEXT**
  - Complete sprite pixel rendering logic
  - Finish sprite 0 hit detection
  - Test with sprite-heavy games
- [X] ~~Integrate GUI event loop with emulation~~ ✅ **90% complete**
- [X] ~~Add keyboard input mapping~~ ✅ **80% complete**
- [🚧] **Complete Mapper 1 (MMC1)** (50% complete) - **HIGH PRIORITY**
  - Implement PRG bank switching
  - Implement CHR bank switching
  - Test with MMC1 games (Metroid, Zelda, Mega Man 2)

### Phase 2: Audio & Additional Mappers
- [X] ~~Implement APU pulse channels~~ ✅ Complete (2026-05-17)
- [X] ~~Implement APU triangle channel~~ ✅ Complete (2026-05-17)
- [X] ~~Implement APU noise channel~~ ✅ Complete (2026-05-17)
- [X] ~~Implement APU DMC~~ ✅ Complete (2026-05-17)
- [ ] Add Mappers 2, 3, 4

### Phase 3: Quality of Life
- [ ] Save state support (serialize all state)
- [ ] ROM browser GUI
- [ ] Pause/reset controls
- [ ] Fast forward / slow motion
- [ ] Screenshot capture

### Phase 4: Advanced Features
- [ ] Debugger (CPU state, breakpoints, step through)
- [ ] PPU viewer (nametables, patterns, palettes)
- [ ] Memory viewer/editor
- [ ] Rewind functionality (ring buffer of states)
- [ ] TAS (Tool-Assisted Speedrun) input recording

### Phase 5: Accuracy & Compatibility
- [ ] Cycle-accurate PPU rendering edge cases
- [ ] PPU open bus behavior
- [ ] Sprite overflow flag accuracy
- [ ] APU sweep unit edge cases
- [ ] More mappers (5, 7, 9, 11, etc.)
- [ ] Pass test ROM suites (blargg's tests, etc.)

---

## References

### NES Hardware Documentation
- NESdev Wiki: https://www.nesdev.org/wiki/
- 6502 Reference: http://www.6502.org/
- Comprehensive NES Emu Guide: https://bugzmanov.github.io/nes_ebook/

### Test ROMs
- nestest.nes - CPU instruction tests
- Blargg's tests - CPU, PPU, APU tests
- https://github.com/christopherpow/nes-test-roms

### Similar Projects
- FCEUX - Full-featured NES emulator
- Nestopia - High accuracy emulator
- Mesen - Debugging-focused emulator

---

## License

TODO: Add license information

---

## Contributing

TODO: Add contribution guidelines

---

**Last Updated:** 2025-11-12
**Emulator Version:** 0.1.0 (pre-alpha)
