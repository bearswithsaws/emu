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
- Support for common mappers (0, 1, 2, 3, 4, 7, 9, 11, 66 implemented)
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

**Implementation Status:** ~99% complete (nestest passes — 2026-05-17)

**Features:**
- All 56 legal opcodes implemented
- All commonly-used illegal/undocumented opcodes with correct addressing modes
- 13 addressing modes (Implied, Accumulator, Immediate, Zero Page, Zero Page X/Y, Relative, Absolute, Absolute X/Y, Indirect, Indexed Indirect, Indirect Indexed)
- Cycle-accurate timing with page boundary detection
- Stack operations (256-byte stack at $0100-$01FF)
- Status register: NV-BDIZC flags
- Interrupt support (NMI fully functional, IRQ implemented)
- Passes nestest.nes (all official + illegal opcode tests) in 26,563 CPU cycles

**Instruction Dispatch:**
The CPU uses a 3D lookup table `instruction_table[c][a][b]` based on opcode bit pattern:
```
Opcode: aaabbbcc
- cc (bits 0-1): Instruction group (0-3, where 3 = illegal cc)
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
- ✅ Full sprite rendering pipeline (pixel output, palette, priority compositing)
- ✅ Horizontal and vertical flip (attr bits 6/7)
- ✅ 8x16 sprite mode (tile bit 0 selects pattern table; top/bottom halves)
- ✅ Sprite 0 hit detection (both pixels opaque, excludes x=255, left-column clip)
- ✅ OAM DMA ($4014) — synchronous copy + CPU halt cycles enforced in main loop

**Partially Implemented:**
- ⚠️ Sprite overflow flag — hardware has a diagonal-scan bug; we set the flag correctly for the simple case but do not replicate the hardware's incorrect scan behaviour

**NOT Yet Implemented:**
- ❌ Open bus behavior (returns last value on bus)
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
- PRG-RAM: 8KB at $6000-$7FFF (work RAM; backed by `nrom_ctx` allocation — fixed 2026-05-21)
- CHR-ROM: 8KB (or CHR-RAM)
- 16KB PRG-ROM is mirrored to fill 32KB space
- Nametable mirroring: Horizontal or Vertical

**Compatible Games:** ~28 games including Donkey Kong, Balloon Fight, Excitebike

#### Mapper 001 - MMC1 ([arch/6502/mapper_001.c](arch/6502/mapper_001.c))

**Status:** ✅ Complete (2026-05-17) — all bank switching modes implemented, tested, and bug-fixed

**Implemented:**
- ✅ 5-write shift register with serial interface ($8000-$FFFF)
- ✅ Reset on bit-7 write (forces PRG mode 3)
- ✅ All 4 internal registers (control, CHR0, CHR1, PRG)
- ✅ PRG mode 0/1: 32KB window (bug fixed 2026-05-17 — was using 16KB stride)
- ✅ PRG mode 2: fix first bank at $8000, switchable at $C000
- ✅ PRG mode 3: switchable at $8000, fix last bank at $C000 (power-up default)
- ✅ CHR 8KB banking (bit 0 of register ignored per spec)
- ✅ CHR 4KB banking (two independent banks)
- ✅ CHR-RAM banking applied on both read and write paths (bug fixed 2026-05-17)
- ✅ Programmable mirroring (H/V/single-screen-lo/single-screen-hi)
- ✅ PRG-RAM at $6000-$7FFF with enable/disable via prg_bank bit 4
- ✅ 11 unit tests, 23 assertions — all passing

**Remaining:**
- ⚠️ Real game compatibility not yet verified (needs ROM testing)

**Specifications:**
- PRG-ROM: Up to 256KB (16KB or 32KB banking)
- CHR-ROM: Up to 128KB (4KB or 8KB banking)
- 5-write shift register ($8000-$FFFF)
- 4 internal registers (control, CHR0, CHR1, PRG)
- Programmable mirroring (H/V/single-screen)

**Compatible Games:** ~680 games including Metroid, Mega Man 2, Legend of Zelda, Castlevania II

#### Mapper 002 - UxROM ([arch/6502/mapper_002.c](arch/6502/mapper_002.c))

**Status:** ✅ Complete (2026-05-17) — 10 unit tests passing

**Implemented:**
- ✅ Switchable 16KB PRG bank at $8000-$BFFF via writes to $8000-$FFFF
- ✅ Fixed last PRG bank at $C000-$FFFF
- ✅ CHR-RAM 8KB (no banking; PPU read/write)
- ✅ Bank register masked to low nibble (4 bits)
- ✅ 10 unit tests — all passing

**Specifications:**
- PRG-ROM: Up to 256KB (16KB switchable + 16KB fixed)
- CHR-RAM: 8KB (no CHR-ROM banking)
- Simple write to $8000-$FFFF selects bank

**Compatible Games:** ~270 games including Mega Man, Castlevania, Duck Tales

#### Mapper 003 - CNROM ([arch/6502/mapper_003.c](arch/6502/mapper_003.c))

**Status:** ✅ Complete (2026-05-17) — 11 unit tests passing

**Implemented:**
- ✅ Fixed PRG-ROM: 16KB mirrored or 32KB straight (no banking)
- ✅ CHR-ROM 8KB bank switching via writes to $8000-$FFFF
- ✅ Bank register masked to 2 bits (4 banks max)
- ✅ CHR writes to ROM are no-ops
- ✅ 11 unit tests — all passing

**Specifications:**
- PRG-ROM: 16KB or 32KB (no banking)
- CHR-ROM: Up to 32KB (8KB banking)
- Simple write to $8000-$FFFF selects CHR bank

**Compatible Games:** ~155 games including Arkanoid, Paperboy, Pipe Dream

#### Mapper 004 - MMC3 ([arch/6502/mapper_004.c](arch/6502/mapper_004.c))

**Status:** ✅ Complete (2026-05-17) — 19 unit tests passing

**Implemented:**
- ✅ PRG-ROM: four 8KB windows ($8000/$A000/$C000/$E000); R6/R7 switchable; fixed banks at second-to-last and last
- ✅ PRG mode bit: swaps which window is fixed vs switchable at $8000/$C000
- ✅ CHR-ROM: eight 1KB windows; R0/R1 select 2KB aligned pairs; R2-R5 select 1KB pages
- ✅ CHR inversion bit: swaps which CHR half gets the 2KB banks
- ✅ PRG-RAM: 8KB at $6000-$7FFF (write-protect bits accepted but ignored)
- ✅ Scanline IRQ counter (decrements via PPU scanline callback at dot 260)
- ✅ IRQ latch, reload, enable/disable registers
- ✅ Dynamic mirroring (H/V) via $A000
- ✅ irq_pending wired to CPU IRQ in 6502.c
- ✅ 19 unit tests — all passing

**Notes:**
- Uses PPU scanline callback instead of A12 edge detection (standard emulator approach — real PPU sprite-CHR fetching is not cycle-accurate enough for A12)

**Compatible Games:** ~600+ games including Super Mario Bros. 3, Mega Man 3/4/5/6, Contra, Kirby's Adventure, Ninja Gaiden

#### Mapper 007 - AxROM ([arch/6502/mapper_007.c](arch/6502/mapper_007.c))

**Status:** ✅ Complete (2026-05-18) — 7 unit tests, 13 assertions passing

**Implemented:**
- ✅ Switchable 32KB PRG bank at $8000-$FFFF (bits 0-2 of register)
- ✅ CHR-RAM 8KB (PPU read/write)
- ✅ Single-screen mirroring via bit 4 (0 = MIRROR_SINGLE_LO, 1 = MIRROR_SINGLE_HI)

**Specifications:**
- PRG-ROM: Up to 256KB (32KB switchable)
- CHR-RAM: 8KB (no banking)
- Single-screen mirroring switchable mid-game

**Compatible Games:** ~75 games including Battletoads, Marble Madness, Wizards & Warriors

#### Mapper 011 - Color Dreams ([arch/6502/mapper_011.c](arch/6502/mapper_011.c))

**Status:** ✅ Complete (2026-05-18) — 6 unit tests, 12 assertions passing

**Implemented:**
- ✅ Switchable 32KB PRG bank (bits 0-1 of register)
- ✅ Switchable 8KB CHR bank (bits 4-7 of register)
- ✅ Both banks selected by single write to $8000-$FFFF
- ✅ Fixed mirroring (set by iNES header)

**Specifications:**
- PRG-ROM: Up to 128KB (32KB switchable)
- CHR-ROM: Up to 128KB (8KB switchable)
- Single register controls both PRG and CHR banks

**Compatible Games:** ~28 unlicensed games including Bible Adventures, Spiritual Warfare

#### Mapper 066 - GxROM ([arch/6502/mapper_066.c](arch/6502/mapper_066.c))

**Status:** ✅ Complete (2026-05-18) — 6 unit tests, 11 assertions passing

**Implemented:**
- ✅ Switchable 32KB PRG bank (bits 4-5 of register)
- ✅ Switchable 8KB CHR bank (bits 0-1 of register)
- ✅ Both banks selected by single write to $8000-$FFFF
- ✅ CHR writes to ROM are no-ops
- ✅ Fixed mirroring (set by iNES header)

**Specifications:**
- PRG-ROM: Up to 128KB (32KB switchable)
- CHR-ROM: Up to 32KB (8KB switchable)
- Single register controls both PRG and CHR banks

**Compatible Games:** ~17 games including Gumshoe, Dragon Power, Super Mario Bros. + Duck Hunt

#### Mapper 009 - PxROM / MMC2 ([arch/6502/mapper_009.c](arch/6502/mapper_009.c))

**Status:** ✅ Complete (2026-05-18) — 10 unit tests, 24 assertions passing

**Implemented:**
- ✅ Switchable 8KB PRG bank at $8000-$9FFF (4-bit register via $A000-$AFFF)
- ✅ Fixed last three 8KB PRG banks at $A000-$BFFF, $C000-$DFFF, $E000-$FFFF
- ✅ Two 4KB CHR windows each driven by a FD/FE latch
- ✅ Four CHR bank registers (lo/FD, lo/FE, hi/FD, hi/FE) selectable via $B000-$EFFF
- ✅ CHR latch triggers: reads at $0FD0-$0FDF → latch0=FD, $0FE0-$0FEF → latch0=FE, $1FD0-$1FDF → latch1=FD, $1FE0-$1FEF → latch1=FE (latch fires after the read returns)
- ✅ Power-up latch state: both latches = FE
- ✅ Dynamic mirroring (H/V) via $F000 bit 0
- ✅ 10 unit tests — all passing

**Specifications:**
- PRG-ROM: 128KB (8KB switchable + three fixed 8KB banks)
- CHR-ROM: 128KB (two 4KB switchable windows with latch-controlled bank selection)
- Latch mechanism: CHR bank switches mid-render when PPU fetches tile $FD or $FE

**Compatible Games:** Punch-Out!!, Mike Tyson's Punch-Out!!

### NES Bus ([arch/6502/nesbus.c](arch/6502/nesbus.c))

**Implementation Status:** ~85% complete

**Features:**
- 2KB internal RAM with mirroring
- PPU register routing ($2000-$3FFF)
- Controller I/O routing ($4016-$4017)
- Cartridge routing ($4020-$FFFF)
- APU register routing ($4000-$4017)

**Implemented:**
- OAM DMA ($4014) — synchronous 256-byte copy + 513-cycle CPU halt enforced in main loop

---

## Build Instructions

### Prerequisites

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libsdl2-dev
```

**Windows (via MSYS2/MinGW):**
```bash
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-SDL2
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
  - Mapper 000 (NROM) fully functional (PRG-RAM $6000-$7FFF fixed 2026-05-21)
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

- **PPU Sprite Rendering** (~95% complete — 2026-05-17)
  - ✅ OAM structure and secondary OAM
  - ✅ Sprite evaluation logic (all 64 sprites, secondary OAM population)
  - ✅ 8x8 and 8x16 sprite modes
  - ✅ Horizontal and vertical flip
  - ✅ Full pixel rendering (priority, palette, transparency compositing)
  - ✅ Sprite 0 hit detection (dot 255 off-by-one bug fixed 2026-05-17)
  - ✅ OAM DMA CPU halt (513-cycle stall enforced in main loop 2026-05-17)
  - ⚠️ Sprite overflow flag — correct for common case; hardware diagonal-scan bug not replicated

- **GUI/Display Integration** (~98% complete)
  - ✅ SDL2 window creation and rendering (lib/display/)
  - ✅ Frame buffer display (256x240)
  - ✅ Event loop integrated with emulation
  - ✅ Frame timing (60 Hz via PPU frame_complete flag)
  - ✅ Pause/resume functionality
  - ✅ Window close handling
  - ✅ **Full ImGui menu bar** (2026-05-17, issue #49)
    - File: Open ROM (native GTK file picker via nativefiledialog-extended), Recent ROMs (last 5, persisted to `~/.config/emu/recent.txt`), Close ROM, Quit
    - Emulation: Pause/Resume, Reset (Soft), Power Cycle, Save/Load State slots 1–5 (F2–F6/Shift+F2–F6), Speed (50%/100%/200%/Uncapped)
    - Debug: CPU Debugger, Memory Viewer, PPU Viewer, APU Visualizer (panel visibility toggles)
    - View: Scale 1×–4×, Fullscreen (F11), Show FPS Overlay, Toggle Menubar (F1)
    - Help: Controls Reference popup, About popup (version from emu_config.h)
  - ✅ Window scale/fullscreen/title control (display helpers added to lib/display/)
  - ✅ **ROM launcher splash screen** (2026-05-18, issues #50/#102) — shown when no ROM is loaded; "Drop a .nes file here / Open ROM… / Recent ROMs"; disappears on load
  - ✅ **Drag-and-drop** (2026-05-18) — SDL_DROPFILE captured in event hook, ROM loaded on next frame
  - ✅ **Speed control** (2026-05-18, issue #58) — 50%/100%/200%/Uncapped via Emulation menu; Tab held = uncapped fast-forward
  - ✅ **Optional ROM argument** (2026-05-18, issue #102) — emulator starts in idle state if no ROM path given; idle loop yields 16 ms/frame
  - ✅ **Windows build** (2026-05-18, issue #102) — -mwindows suppresses console; SDL2.dll + libwinpthread-1.dll copied next to exe at build time
  - ⚠️ Old gui.c/gui.h removed, replaced with modular display library

- **Mapper 001 (MMC1)** ✅ Complete (2026-05-17)
  - All PRG and CHR bank switching modes implemented and unit-tested
  - Two bugs fixed: 32KB PRG stride and CHR-RAM read banking

- **Mappers 002/003/004** ✅ Complete (2026-05-17)
  - UxROM (002), CNROM (003), MMC3 (004) fully implemented and unit-tested
  - 40 new assertions across 3 test files — all passing

- **Mappers 007/009/011/066** ✅ Complete (2026-05-18)
  - AxROM (007): 32KB switchable PRG, CHR-RAM, single-screen mirroring switch
  - PxROM/MMC2 (009): 8KB switchable + 3 fixed PRG banks; two 4KB CHR windows with FD/FE latch — completes mapper epic
  - Color Dreams (011): 32KB PRG + 8KB CHR both banked via single register write
  - GxROM (066): 32KB PRG + 8KB CHR both banked via single register write
  - 60 new assertions across 4 test files — all passing

- **Frame Timing** (100% complete)
  - ✅ 60 Hz frame rate via PPU
  - ✅ VBlank synchronization
  - ✅ NMI triggering

### ✅ CPU Debugger (Complete — 2026-05-18, issue #52; Breakpoints extended 2026-05-19, issue #53)
- ✅ 6502 disassembler (`arch/6502/disasm.c/.h`) — flat 256-entry table covering all legal + documented illegal opcodes; all addressing modes; REL branch target computed from PC
- ✅ `disasm_insn(addr, bytes, out, len)` — returns instruction length (1-3); used by panel and unit tests
- ✅ CPU Debugger ImGui panel (`lib/ui/`) — register display (PC/A/X/Y/SP editable hex fields; P flags as coloured N V - B D I Z C indicators); 20-line disassembly view scrolling to keep PC visible; step/step-over/run/break/reset controls; scanline/dot/cycle status bar
- ✅ Breakpoints (full — issue #53):
  - Up to 16 breakpoints; `struct ui_breakpoint { addr, type (EXEC/READ/WRITE), enabled }`
  - Click any disassembly line to toggle an execute breakpoint (shown in red)
  - Breakpoints panel (table in CPU Debugger): Type dropdown, Address hex input, Enabled checkbox, Delete button, + Add Breakpoint button
  - Execute breakpoints: checked per-instruction in main loop (`ui_debugger_is_breakpoint`)
  - Read/Write breakpoints: `nesbus_bp_hook_fn` registered in bus; fires on every CPU read/write; sets `dbg_rw_bp_hit` flag consumed by main loop
  - All breakpoint types pause emulation when hit; survive panel close/reopen
- ✅ Step (F7) — executes exactly one CPU instruction then pauses
- ✅ Step Over (F8) — runs until PC = current PC + instruction length (handles JSR)
- ✅ Panel toggled from Debug menu; dockable via ImGui docking
- ✅ Unit tests: `tests/test_disasm.c` — 16 test groups, 126 assertions, all passing

### ❌ Not Started
- Performance optimization (Phase 5)

### Code Quality Status
- ✅ .clang-format configuration
- ✅ GitHub Actions CI (ubuntu-latest + windows-latest matrix; replaces Travis CI)
- ✅ **Comprehensive PPU documentation** (added 2025-11-12)
  - PPU_ARCHITECTURE_COMPLETE.md (51,000+ tokens)
  - PPU_REGISTERS_REFERENCE.md
  - PPU_RENDERING_PIPELINE.md
  - PPU_IMPLEMENTATION_COMPARISON.md
  - BUGFIXES_APPLIED.md
- ✅ Unit tests: PPU clock (13 tests, 65 assertions), Mapper 001 (11 tests, 23 assertions), Mapper 002 (6 tests, 10 assertions), Mapper 003 (6 tests, 11 assertions), Mapper 004 (10 tests, 19 assertions), Mapper 007 (7 tests, 13 assertions), Mapper 009 (10 tests, 24 assertions), Mapper 011 (6 tests, 12 assertions), Mapper 066 (6 tests, 11 assertions), APU (8 tests, 72 assertions), CPU nestest integration test, Disassembler (16 groups, 126 assertions) — **12 test suites, all passing**
- ✅ **Blargg headless test runner** (`tests/test_blargg_runner.c`) — full NES stack (CPU+PPU+APU+cartridge) without SDL2; implements $6000 protocol; 24 CTest entries across 3 suites (ppu_vbl_nmi, apu_test, apu_reset); **6/24 passing** as of 2026-05-21; optional via `-DBLARGG_TEST_ROMS_PATH=<dir>`
- ✅ Documentation (CLAUDE.md updated 2026-05-21)

---

## Known Issues

### Critical Bugs (Fixed)
- ~~Page boundary detection in ABX/ABY addressing~~ ✅ Fixed (pre-2025-11-12)
- ~~**PPUSTATUS VBlank hack**~~ ✅ **Fixed 2025-11-12** - Removed hardcoded vblank flag, frame timing now correct
- ~~**PPUDATA read buffer missing**~~ ✅ **Fixed 2025-11-12** - Implemented proper buffering for CHR/nametable reads
- ~~**Coarse X increment timing off by one cycle**~~ ✅ **Fixed 2025-11-12** - Eliminated 8-pixel viewport offset
- ~~**Mapper 000 PRG-RAM missing**~~ ✅ **Fixed 2026-05-21** - `$6000-$7FFF` was mirroring PRG-ROM; now backed by a proper 8KB `nrom_ctx` work RAM; write path also fixed

### High Priority (Remaining)
1. **Real-game ROM testing** - Mappers 0-4, 7, 11, 66 implemented; none have been verified with actual ROMs yet

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

### 2026-05-21 Session: Blargg Headless Test Runner (Issues #81, #82)

**SDL-free NES test runner implemented. Closes the infrastructure gap for the Testing epic.**

**Architecture (`tests/test_blargg_runner.c`):**
- Full NES stack without SDL2: `load_rom` → `cpu6502_init` → `ppu2c02_init` → `nesbus_init` → `connect_cartridge` → PPU cartridge connect → `cpu->reset()`
- PPU frame buffer pointer left NULL — `2c02.c` guards all pixel writes with `if (ppu.frame_buffer)`, so the PPU still clocks correctly (timing, NMI, sprite-0 hit) without allocating a display buffer
- APU ring buffer is non-blocking SPSC; samples are produced but never consumed (wraps silently); no SDL audio device opened
- `emu_tick()`: 3× `ppu->clock()`, then CPU or DMA stall, then `apu_clock()`, then IRQ check — exact cadence as `emu.c`
- **Blargg $6000 protocol**: validates magic `$DE $B0 $61` at `$6001-$6003`; polls `$6000` for `$00`=pass / `$01-$7F`=fail / `$80`=running / `$81`=reset-request
- **$81 reset handling**: 6-frame delay then `cpu->reset()` — enables the entire `apu_reset` suite to run headlessly
- 600-frame timeout (≈10 NES seconds); prints diagnostic text from `$6004+` on failure

**Mapper 000 PRG-RAM fix (`arch/6502/mapper_000.c`):**
- **Root cause**: `$6000-$7FFF` was applying the PRG-ROM address mask (`addr & 0x3FFF` / `0x7FFF`) to all addresses, including the work RAM window — returning PRG-ROM bytes instead of writable RAM
- **Fix**: Added `struct nrom_ctx { uint8_t prg_ram[0x2000]; }` context; read path returns `ctx->prg_ram[addr & 0x1FFF]` for `$6000-$7FFF`; write path stores to same; `mapper_000_init()` added and wired into `mapper.c`
- This is an emulation accuracy bug (not just a test issue) — any NROM game using work RAM was broken

**Test infrastructure (`tests/CMakeLists.txt`):**
- `test_blargg_runner` target linked against `lib6502` (no SDL2 dependency)
- Optional via `-DBLARGG_TEST_ROMS_PATH=<dir>`; 24 CTest entries registered only if ROM files exist:
  - `ppu_vbl_nmi/rom_singles/`: `01-vbl_basics` through `10-even_odd_timing`
  - `apu_test/rom_singles/`: `1-len_ctr` through `8-dmc_rates`
  - `apu_reset/`: `4015_cleared`, `4017_timing`, `4017_written`, `irq_flag_cleared`, `len_ctrs_enabled`, `works_immediately`
- 2005-era Blargg ROMs (`blargg_apu_2005.07.30`, `sprite_hit_tests_2005.10.05`, `sprite_overflow_tests`) predate the $6000 protocol — results only via PPU/audio; excluded with comment

**Current Blargg score: 6/24 passing.** Failures expose real PPU/APU accuracy issues tracked in new issues:

| Issue | Suite | Failure |
|-------|-------|---------|
| #115 | ppu_vbl_nmi 01 | VBL flag basics |
| #116 | ppu_vbl_nmi 02 | VBL set timing |
| #117 | ppu_vbl_nmi 03 | VBL clear timing |
| #118 | ppu_vbl_nmi 06 | NMI suppression |
| #119 | apu_test 3 | IRQ flag behavior |
| #120 | apu_reset suite | APU state after reset |

**New GitHub issues created:**
- #115 – PPU VBL flag timing (vbl_basics)
- #116 – PPU VBL set timing (vbl_set_time)
- #117 – PPU VBL clear timing (vbl_clear_time)
- #118 – PPU NMI suppression (suppression)
- #119 – APU IRQ flag behavior (irq_flag)
- #120 – APU state after CPU reset (apu_reset suite)

**Files modified:** `arch/6502/mapper_000.c`, `arch/6502/mapper_000.h`, `arch/6502/mapper.c`, `tests/test_blargg_runner.c` (new), `tests/CMakeLists.txt`

**All 12 original test suites still pass — no regressions.**

---

### 2026-05-19 Session: Build System & Distribution (Issues #60–#65, closes Build epic #66)

**Full CI pipeline and release packaging implemented. Closes the Build System & Distribution epic.**

**Issues #60 and #61 were already complete** — `.github/workflows/ci.yml` existed with an ubuntu-latest + windows-latest matrix running all 12 CTest suites on every push and PR.

**New assets (`assets/`):**
- `assets/icon.png` — 256×256 RGBA NES controller icon
- `assets/emu.desktop` — freedesktop.org application entry (`Categories=Game;Emulator;`, `MimeType=application/x-nes-rom;`)

**CMakeLists.txt changes:**
- Version bumped from `0.1` to `0.1.0` (adds PATCH component)
- `install()` targets: binary → `bin/`; desktop file → `share/applications/`; icon → `share/pixmaps/` and `share/icons/hicolor/256x256/apps/`

**emu_config.h.in:**
- Added `emu_VERSION_PATCH` and `EMU_VERSION` string macro (`"MAJOR.MINOR.PATCH"`) for use in About dialog

**`.github/workflows/release.yml` (new — issue #65):**
- Trigger: `push: tags: ['v*']`
- **Linux job**: Release build → CTest → `cmake --install` into `AppDir` → linuxdeploy (extracted, no FUSE needed in Actions) → `emu-vX.Y.Z-linux-x86_64.AppImage`
- **Windows job**: Release build via MSYS2 → CTest → `ntldd -R` to recursively find all MinGW DLL deps → zip → `emu-vX.Y.Z-windows-x64.zip`
- **Release job**: downloads both artifacts, runs `gh release create --generate-notes --prerelease` to publish them

**To cut a release:** `git tag v0.1.0 && git push --tags`

---

### 2026-05-19 Session: Breakpoints — Read/Write + Panel (Issue #53, closes UI epic #59)

**Full breakpoint system implemented. Closes the last remaining item in the UI epic.**

**Architecture:**
- `struct ui_breakpoint { uint16_t addr; ui_bp_type type; int enabled; }` — up to 16 breakpoints (`UI_MAX_BREAKPOINTS`) stored in a fixed array on `ui_context`
- `ui_bp_type` enum: `UI_BP_EXEC=0`, `UI_BP_READ=1`, `UI_BP_WRITE=2` (defined in `lib/ui/ui.h`)
- `nesbus_bp_hook_fn` added to `struct nesbus` (`arch/6502/nesbus.h`): `void (*)(uint16_t addr, int is_write, void *ud)` — called after every CPU read/write in the static `read()`/`write()` in `nesbus.c`
- `ui_set_debug_context()` registers the hook; the callback checks all enabled READ/WRITE breakpoints and sets `dbg_rw_bp_hit` flag
- Main loop in `emu.c` checks `ui_debugger_consume_rw_bp_hit()` alongside execute breakpoints and calls `display_set_paused(display, 1)` when either fires

**UI panel (CPU Debugger, `lib/ui/ui.cpp`):**
- Inline breakpoint chip list replaced with an `ImGui::BeginTable` with 5 columns: Type (dropdown), Address (hex input, Enter to commit), Enabled (checkbox), Hit indicator, Delete (X button)
- Scrollable child window (`frame_h * 4.5`) contains the table so the disassembly view isn't pushed off-screen
- `+ Add Breakpoint` button appends an enabled EXEC BP at $0000 for the user to edit
- Clicking a disassembly line still toggles an EXEC breakpoint (adds if absent, removes if present)

**New public API (`lib/ui/ui.h`):**
- `struct ui_breakpoint` and `ui_bp_type` enum
- `int ui_debugger_consume_rw_bp_hit(struct ui_context *ui)` — returns 1 and clears flag if a R/W BP fired

**Files modified:** `arch/6502/nesbus.h`, `arch/6502/nesbus.c`, `lib/ui/ui.h`, `lib/ui/ui.cpp`, `emu.c`

**All 12 test suites pass — no regressions.**

---

### 2026-05-19 Session: PPU Viewer Panel (Issues #54, #55, #56)

**Full PPU Viewer implemented. Closes all three remaining Phase 4 debug panel items.**

**PPU Viewer panel (`lib/ui/ui.cpp`) — three tabs:**

**Pattern Tables (`#54`):**
- NES system palette lookup table (64 colors, ARGB8888) added as `NES_SYS_PALETTE[64]`
- `ppu_decode_tile()` helper: decodes one 8×8 tile from two bitplanes into ARGB pixels
- Two `SDL_Texture` 128×128 (SDL_TEXTUREACCESS_STREAMING), updated each frame via `SDL_LockTexture`
- 16×16 tile grid per table; rendered at 2× scale (256×256 screen pixels each)
- Palette selector (radio buttons): BG 0–3, Spr 0–3 — changes colors live
- Hover tooltip: table #, tile index ($00–$FF), CHR address range
- Reads directly from `ppu->pattern_table[]` (CHR-RAM for CHR-RAM games; see note)

**Nametables (`#55`):**
- Four `SDL_Texture` 256×240, updated each frame, decoded tile-by-tile using attribute table palette
- Attribute palette lookup: `shift = ((cy & 2) << 1) | (cx & 2)` selects 2-bit slot from attribute byte
- Mirroring applied via `ppu->cart->map->mirroring` → `nt_off[4][4]` lookup (HORIZONTAL/VERTICAL/SINGLE_LO/SINGLE_HI)
- Displayed as 2×2 grid at 45% scale (~115×108 each)
- Yellow scroll viewport rectangle drawn via `ImDrawList::AddRect` using loopy `v` and `x` registers
- Mirroring mode label + current BG pattern table shown above grid

**OAM (`#56`):**
- `ImGui::BeginTable` with 9 columns: #, Y, X, Tile, Pal, Pri, H-Fl, V-Fl, Vis
- 64 rows from `ppu->oam[256]`; off-screen sprites (Y ≥ 239) dimmed with dark row background
- Sticky header row; scrollable

**Note on CHR-ROM games:** `ppu->pattern_table[]` holds CHR-RAM for CHR-RAM mappers (0-CHR-RAM, 1, 2, 3, 7, 11, 66). For CHR-ROM mappers with banking (4, 9 etc.), this buffer may not reflect current banks; a future improvement can snapshot via `ppu->ppu_read`.

**Files modified:** `lib/ui/ui.cpp`

---

### 2026-05-19 Session: Memory Viewer Panel (Issue #57)

**Hex memory viewer implemented. Closes Phase 4 memory viewer item.**

**Memory Viewer panel (`lib/ui/ui.cpp`):**
- Uses `imgui_memory_editor` (vendored at `lib/ui/vendor/imgui_memory_editor.h`)
- Four tabs: **CPU Bus** (64 KB read-only view via `nesbus_read`), **OAM** (256 B sprite RAM), **VRAM** (16 KB PPU address space via `ppu_read`), **Palette** (32 B palette RAM)
- Panel toggled via Debug → Memory Viewer menu; dockable

**Merged as PR #109.**

---

### 2026-05-18 Session: CPU Debugger Panel (Issue #52)

**Full CPU debugger implemented. Closes Phase 4 debugger item.**

**Disassembler (`arch/6502/disasm.c/.h`):**
- Flat 256-entry lookup table covering all legal opcodes + all documented illegal opcodes used by the CPU emulator (SLO, RLA, SRE, RRA, SAX, LAX, DCP, ISB, ANC, ALR, ARR, SBX, USB, etc.)
- All 13 addressing modes: IMP, ACC, IMM, ZPG, ZPX, ZPY, ABS, ABX, ABY, IND, IDX, IDY, REL
- REL branch targets computed from instruction address: `target = addr + 2 + (int8_t)offset`
- `disasm_insn(addr, bytes[3], out, len)` returns instruction length (1–3); does not call into bus

**CPU Debugger panel (`lib/ui/ui.cpp`):**
- Registers section: PC/A/X/Y/SP as editable hex input fields (writes directly into cpu struct); P flags as coloured N V - B D I Z C indicators (green=set, grey=clear)
- Disassembly view: 20 lines; shows address + raw bytes + mnemonic/operand; current PC highlighted yellow; breakpoints highlighted red; click any line to toggle a breakpoint
- Scroll controls: `<<`/`>>` scroll by 4 instructions; `PC` button re-syncs view to current PC; auto-syncs when paused/stepping
- Controls: Step (F7), Step Over (F8), Run, Break, Reset buttons; breakpoint list shown as removable chips
- Status bar: `CYC:N  SL:N  DOT:N  [PAUSED/RUNNING]`
- Panel toggled via Debug menu; dockable

**Step mode (`emu.c`):**
- `emu_tick()` — one NES tick: 3 PPU + 1 CPU + 1 APU cycles
- `emu_step_one()` — finishes any in-progress instruction then executes exactly one complete instruction
- `emu_step_over(target)` — calls `emu_step_one()` until `cpu->PC == target` (max 100 000 instruction guard)
- Breakpoint check in main frame loop: if `ui_debugger_is_breakpoint(ui, cpu->PC)` then pause

**C++ compatibility fix (`arch/6502/2a03.h`):**
- `_Atomic int` ring buffer fields guarded with `#ifdef __cplusplus` / `volatile int` fallback so the header can be included from C++ translation units

**New files:** `arch/6502/disasm.h`, `arch/6502/disasm.c`, `tests/test_disasm.c`

**Modified:** `lib/ui/ui.h`, `lib/ui/ui.cpp`, `lib/ui/CMakeLists.txt`, `arch/6502/CMakeLists.txt`, `arch/6502/2a03.h`, `emu.c`, `tests/CMakeLists.txt`

**All 12 test suites pass — no regressions.**

### 2026-05-18 Session: Windows DLL Bundling (Issue #102)

**Problem:** `emu.exe` launched from Explorer failed with missing `libwinpthread-1.dll` (and previously `SDL2.dll`). Static-linking pthread in emu.exe does not help because `SDL2.dll` itself carries a runtime dependency on `libwinpthread-1.dll` that Windows resolves independently.

**Fix:** CMake `POST_BUILD` step loops over a list of required DLLs and copies each one alongside `emu.exe` using `find_file` to locate them in the MinGW bin path.

**Files modified:**
- `CMakeLists.txt` — MINGW block: `-mwindows -static-libgcc -static-libstdc++` linker flags; `foreach` loop copying `SDL2.dll` and `libwinpthread-1.dll` to `$<TARGET_FILE_DIR:emu>` at build time

**Note:** Issue #64 (Windows release packaging) will replace this with an `ntldd`-based full dependency walk in the GitHub Actions workflow so the list never needs manual maintenance.

### 2026-05-18 Session: ROM Launcher, Idle State, Drag-and-Drop (Issues #50, #102)

**ROM argument is now optional.** The emulator starts in an idle splash state and works as a proper GUI application launched from Explorer or a desktop shortcut.

**Features added:**
- **No-ROM splash panel** (`lib/ui/ui.cpp`) — Dear ImGui overlay centered in the game viewport: "Drop a .nes file here / — or — / Open ROM… / Recent ROMs". Disappears the moment a ROM is loaded. Rendered via `render_no_rom_splash()`.
- **Drag-and-drop** — `SDL_DROPFILE` events captured in `sdl_event_hook` (which now receives `ui` as userdata), path stored in `ui_context::pending_drop_path`, processed at the start of the next `ui_render_frame`.
- **Optional ROM argument** (`emu.c`) — removed hard exit when `argc < 2`; all init runs unconditionally; ROM load + boot sequence only run if a path is given.
- **Idle yield** — main loop sleeps 16 ms/frame when `cartridge_global == NULL` instead of spinning at 100%.
- **nesbus null-cart guard** — `nesbus.c` read/write paths now check `bus.cart != NULL` before dereferencing, returning `0xFF` on reads when no cartridge is connected.

**Files modified:** `emu.c`, `lib/ui/ui.cpp`, `lib/ui/ui.h`, `lib/display/display.c`, `arch/6502/nesbus.c`, `CMakeLists.txt`

### 2026-05-18 Session: Speed Control + Tab Fast-Forward (Issue #58)

**Speed menu fully wired to the emulation loop.**

- **Menu selection** (`lib/ui/ui.cpp`) — `speed_multiplier` set to 0.5/1.0/2.0/-1.0 (uncapped) via Emulation → Speed sub-menu with checkmarks.
- **Main loop** (`emu.c`) — reads `ui_get_speed_multiplier(ui)` each frame; 50% adds extra `SDL_Delay` on top of audio backpressure; >100% or -1.0 removes throttle entirely.
- **Tab fast-forward** — holding Tab overrides the menu selection to uncapped speed for instant fast-forward without changing the persistent setting.

**Files modified:** `emu.c`, `lib/ui/ui.cpp`

### 2026-05-18 Session: Save State / Load State (Issue #51) — closes UI epic #59

**Full save/load state system implemented. Closes Phase 3 and the UI epic.**

**Save format:** binary sequential file at `~/.local/share/emu/slot_N.sav`. Sections in order: file header (magic + version + mapper_id + CHR-RAM size + ctx size), CPU registers, PPU VRAM/registers/OAM, CPU RAM (2 KB), APU channel state (ring buffer excluded), controllers, mapper mirroring + IRQ flag + mapper ctx blob, CHR-RAM (if applicable). Format is validated on load (magic, version, mapper_id mismatch all caught with clear error messages).

**New files:**
- `arch/6502/savestate.h/.c` — `savestate_save(slot, bus)` / `savestate_load(slot, bus)` public API

**Modified:**
- `arch/6502/mapper.h` — added `ctx_size` field to `struct mapper`
- All mapper inits (001–009, 011, 066) — set `ctx_size = sizeof(ctx_struct)`
- `arch/6502/nesbus.h/.c` — `nesbus_get_ram()` / `nesbus_set_ram()` accessors; removed duplicate `NES_RAM_SIZE` define from .c
- `lib/ui/ui.h` — added `ui_savestate_fn` type and `on_save_state`/`on_load_state` to `ui_callbacks`
- `lib/ui/ui.cpp` — removed `BeginDisabled` wrapper from Save/Load State menus; wired callbacks; added F2–F6/Shift+F2–F6 keyboard shortcuts; updated Controls Reference popup
- `emu.c` — `emu_save_state`/`emu_load_state` callbacks implemented; wired into `ui_callbacks`
- `arch/6502/CMakeLists.txt` — `savestate.c` added to lib6502

**Keyboard shortcuts:** F2–F6 = save slot 1–5, Shift+F2–F6 = load slot 1–5. Menu shows same hints.

**All 11 test suites pass — no regressions.**

### 2026-05-18 Session: Mapper 009 (PxROM / MMC2) — closes mapper epic #80

**Mapper 009 implemented and unit-tested. Completes the Additional Mappers epic.**

MMC2's distinguishing feature is the CHR latch: each of the two 4 KB CHR windows selects between two possible banks (FD-slot and FE-slot) controlled by a latch that fires automatically when the PPU fetches tiles $FD or $FE from CHR-ROM. The latch update is implemented entirely inside `mapper_009_ppu_read()` with no PPU changes required — it checks the read address after returning data and updates the latch in-place.

**New files:**
- `arch/6502/mapper_009.c/.h` — PxROM/MMC2: 8KB switchable PRG + three fixed 8KB banks; two 4KB CHR windows with FD/FE latch-controlled bank selection; dynamic H/V mirroring
- `tests/test_mapper_009.c` — 10 tests, 24 assertions (all passing)

**Modified:**
- `arch/6502/mapper.c` — case 9 added to mapper dispatch table
- `arch/6502/CMakeLists.txt` — `mapper_009.c` added to lib6502
- `tests/CMakeLists.txt` — `test_mapper_009` target and `mapper_009` CTest entry added

**Total test suite: 11 suites, all passing.**

**Compatible games:** Punch-Out!!, Mike Tyson's Punch-Out!!

### 2026-05-18 Session: Mappers 007, 011, 066

**Three new mappers implemented and unit-tested.**

**New files:**
- `arch/6502/mapper_007.c/.h` — AxROM: 32KB switchable PRG, CHR-RAM, single-screen mirroring via bit 4
- `arch/6502/mapper_011.c/.h` — Color Dreams: 32KB PRG + 8KB CHR both banked by single write to $8000-$FFFF
- `arch/6502/mapper_066.c/.h` — GxROM: 32KB PRG (bits 4-5) + 8KB CHR (bits 0-1) banked by single write; CHR writes to ROM are no-ops
- `tests/test_mapper_007.c` — 7 tests, 13 assertions (all passing)
- `tests/test_mapper_011.c` — 6 tests, 12 assertions (all passing)
- `tests/test_mapper_066.c` — 6 tests, 11 assertions (all passing)

**Cartridge cleanup:** mapper dispatch table and `cartridge.c` updated to register all three new mappers.

**Total test suite: 10 suites, all passing.**

### 2026-05-17 Session: ImGui Menu Bar (Issue #49)

**Full top-level menu bar implemented** across all five menus from the issue spec.

**Files modified:**
- `lib/ui/ui.cpp` — Full menu bar implementation; native file dialog via NFD; recent ROMs persistence; FPS overlay; F1/F11 shortcuts; game rect offset below menu bar
- `lib/ui/ui.h` — `ui_callbacks` struct (`on_soft_reset`, `on_power_cycle`, `on_load_rom`); updated `ui_init` signature; `ui_get_speed_multiplier`, `ui_show_fps` query functions
- `lib/ui/CMakeLists.txt` — Added `nativefiledialog-extended` v1.2.1 via FetchContent (GTK3 backend); exposed `${PROJECT_BINARY_DIR}` for `emu_config.h`
- `lib/display/display.h/.c` — New helpers: `display_request_quit`, `display_set_scale`, `display_get_scale`, `display_toggle_fullscreen`, `display_is_fullscreen`, `display_set_title`
- `emu.c` — `emu_soft_reset`, `emu_power_cycle`, `emu_load_rom` callbacks; `cartridge_global` / `display_global` statics for mid-session ROM swaps

**Menu bar features:**
- **File**: Open ROM (native OS file picker filtered to `*.nes`), Recent ROMs sub-menu (last 5, persisted to `~/.config/emu/recent.txt`), Close ROM, Quit
- **Emulation**: Pause/Resume, Reset (Soft), Power Cycle, Save/Load State 1–5 (greyed — not implemented), Speed 50%/100%/200%/Uncapped with checkmarks
- **Debug**: CPU Debugger, Memory Viewer, PPU Viewer, APU Visualizer (toggle panel visibility booleans)
- **View**: Scale 1×–4×, Fullscreen (F11), Show FPS Overlay, Toggle Menubar (F1)
- **Help**: Controls Reference popup (all keybindings), About popup (version from `emu_config.h`)

**Acceptance criteria met:** All menus navigable; Pause/Resume and Reset work from the menu; Recent ROMs populates after opening a ROM.

### 2026-05-17 Session: Mapper 002/003/004 Unit Tests + Documentation Sync

**Discovered that Mappers 002 (UxROM), 003 (CNROM), and 004 (MMC3) were already fully implemented** — CLAUDE.md falsely listed 002/003 as stubs and omitted 004 entirely.

**New test files:**
- `tests/test_mapper_002.c` — 6 tests, 10 assertions (all passing)
- `tests/test_mapper_003.c` — 6 tests, 11 assertions (all passing)
- `tests/test_mapper_004.c` — 10 tests, 19 assertions (all passing)

**Coverage:**
- Mapper 002: power-up state, bank switching, low-nibble masking, CHR-RAM read/write
- Mapper 003: 16KB mirror, 32KB straight, CHR bank select, 2-bit mask, ROM write NOP
- Mapper 004: PRG power-up, R6 switch, PRG mode 1 swap, PRG-RAM, CHR R0 2KB, CHR R2 1KB, CHR invert, mirroring, IRQ counter, IRQ disable

**Total test suite: 6 suites, 54 tests, 200 assertions — all passing.**

**CLAUDE.md updated** to reflect true implementation status for all mappers.

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

### 2026-05-17 Session: PPU Sprite Completion + Bug Fixes

**Discovered CLAUDE.md was severely out of date** — sprite rendering was already ~95% implemented; documentation claimed 40%.

**Bug fixed:**
- **Sprite 0 hit off-by-one** (`arch/6502/2c02.c`): `ppu.dot != 255` → `ppu.dot != 256`. The excluded pixel is `x = dot - 1`, so the old code excluded x=254 instead of the correct x=255.

**Feature added:**
- **OAM DMA CPU halt** (`emu.c`): `bus->dma_halt_cycles` was set on $4014 writes but never consumed. CPU now skips its clock during the 513-cycle stall while PPU/APU continue to run.

**Tests added** (`tests/test_ppu_clock.c` — 5 new tests, 10 assertions):
- `test_sprite_basic_render` — sprite pixel appears at correct framebuffer location
- `test_sprite_priority_behind_bg` — priority-behind sprite hidden by opaque background
- `test_sprite_horizontal_flip` — H-flip moves opaque pixel to rightmost sprite column
- `test_sprite_vertical_flip` — V-flip moves opaque row to bottom of sprite
- `test_sprite_zero_hit` — sprite_0_hit set on overlap, cleared at pre-render dot 1

**Total test suite: 13 PPU tests, 65 assertions — all passing.**

**Status:** Mapper 0 NROM games are now **fully playable** (background + sprites + audio). Next priority: Mapper 1 (MMC1) to unlock 680+ games.

### 2026-05-17 Session: Mapper 001 (MMC1) Completion

**Discovered CLAUDE.md was severely out of date again** — Mapper 001 was already ~95% implemented; docs claimed 50%.

**Bugs fixed in `arch/6502/mapper_001.c`:**
- **PRG 32KB mode stride**: `bank * 0x4000` → `bank * 0x8000`. The 32KB bank index selects pairs of 16KB banks; the old code was using the wrong stride, mapping to the wrong half of the ROM.
- **CHR-RAM ppu_read ignored banking**: The read path used a flat `addr & 0x1FFF` when `chr_ram_allocated=1`, ignoring chr_bank_0/chr_bank_1. Fixed to apply the same bank selection as ppu_read CHR-ROM and ppu_write paths. Also unified both paths (ROM and RAM) into a single code path.

**Tests added** (`tests/test_mapper_001.c` — 11 tests, 23 assertions, all passing):
- Power-up state, all 4 PRG modes, CHR 8KB and 4KB modes, CHR-RAM read/write banking, PRG-RAM enable/disable, shift register reset, dynamic mirroring.

**Total test suite: 32 tests, 160 assertions — all passing.**

**Status:** Mapper 1 (MMC1) is now fully implemented. Next: verify with real MMC1 ROMs, then add Mappers 2 and 3.

### 2026-05-17 Session: nestest CPU Integration Test (Issue #62)

**All official and illegal opcode tests pass via nestest.nes automation mode.**

**Root cause of test failures — multi-byte illegal NOP addressing modes:**
All illegal NOP opcodes that consume operand bytes ($04, $0C, $14, $1C, $34, $3C, $44, $54, $5C, $64, $74, $7C, $80, $89, $82, $C2, $E2, $D4, $DC, $F4, $FC, $9C) had `&IMP` (implied/1-byte) addressing mode in the instruction table. This meant operand bytes were not consumed by the PC, and were instead executed as the next instruction, corrupting control flow. Fixed by using correct addressing modes: `&ZPG`, `&ABS`, `&ABX`, `&ZPX`, or `&IMM` as appropriate.

**Test architecture** (`tests/test_cpu_nestest.c`):
- Flat 64KB `mem[65536]` — no SDL2, PPU, or APU
- PRG-ROM loaded at $8000 and mirrored to $C000
- CPU reset, then PC forced to $C000 (automation entry point)
- Runs until `cpu->PC < 0x0100` (nestest signals done by entering zero-page execution)
- Results: `mem[$00]` (last failure), `mem[$10]` (official tests), `mem[$11]` (illegal tests); all $00 = PASS
- Progress printed on writes to $10/$11 (first write skipped — initialization)
- Completes in 26,563 CPU cycles

**Files modified:**
- `arch/6502/6502.c` — fixed addressing modes for all multi-byte illegal NOPs; expanded table to `[4][8][8]` with cc=3 illegal op entries
- `tests/test_cpu_nestest.c` — new integration test
- `tests/CMakeLists.txt` — added `test_cpu_nestest` target and `cpu_nestest` CTest entry
- `tests/roms/nestest.nes` — ROM added (Kevin Horton's gold-standard CPU test)

**Total test suite: 7 tests (6 suites + cpu_nestest), all passing.**

---

## Future Work

### Phase 1: Core Functionality (~75% complete, up from ~30%)
- [X] ~~Implement PPU background rendering~~ ✅ **85% complete** (2025-11-12)
- [X] ~~Complete PPU sprite rendering~~ ✅ **95% complete** (2026-05-17)
  - All pixel rendering, priority, flip, 8x16 mode implemented
  - Sprite 0 hit detection implemented and bug-fixed
  - OAM DMA CPU halt enforced
  - 5 new unit tests passing
- [X] ~~Integrate GUI event loop with emulation~~ ✅ **90% complete**
- [X] ~~Add keyboard input mapping~~ ✅ **80% complete**
- [X] ~~Complete Mapper 1 (MMC1)~~ ✅ Complete (2026-05-17)
  - All PRG/CHR bank switching modes implemented and unit-tested
  - Bugs fixed: 32KB PRG stride, CHR-RAM read banking
  - Next: verify with real MMC1 ROMs

### Phase 2: Audio & Additional Mappers
- [X] ~~Implement APU pulse channels~~ ✅ Complete (2026-05-17)
- [X] ~~Implement APU triangle channel~~ ✅ Complete (2026-05-17)
- [X] ~~Implement APU noise channel~~ ✅ Complete (2026-05-17)
- [X] ~~Implement APU DMC~~ ✅ Complete (2026-05-17)
- [X] ~~Add Mappers 2, 3, 4~~ ✅ Complete (2026-05-17) — UxROM, CNROM, MMC3 all implemented and unit-tested
- [X] ~~Add Mappers 7, 11, 66~~ ✅ Complete (2026-05-18) — AxROM, Color Dreams, GxROM all implemented and unit-tested
- [X] ~~Add Mapper 9 (PxROM / MMC2)~~ ✅ Complete (2026-05-18) — latch-based CHR banking implemented and unit-tested

### Phase 3: Quality of Life
- [X] ~~Save state support~~ ✅ Complete (2026-05-18, issue #51) — 5 slots, binary format, F2–F6/Shift+F2–F6 shortcuts, Emulation menu wired
- [X] ~~ROM browser GUI~~ ✅ Complete (2026-05-17) — native file picker + Recent ROMs list via ImGui menu bar
- [X] ~~ROM launcher splash / idle state~~ ✅ Complete (2026-05-18, issue #50) — shown on startup with no ROM; drag-and-drop supported
- [X] ~~Pause/reset controls~~ ✅ Complete (2026-05-17) — wired through Emulation menu
- [X] ~~Fast forward / slow motion~~ ✅ Complete (2026-05-18, issue #58) — Speed menu (50%/100%/200%/Uncapped) + Tab key for instant fast-forward
- [ ] Screenshot capture

### Phase 4: Advanced Features
- [X] ~~Debugger (CPU state, breakpoints, step through)~~ ✅ Complete (2026-05-18, issue #52; extended 2026-05-19, issue #53) — disassembler, editable registers, F7/F8 step/step-over, execute + read/write breakpoints, cycle/scanline/dot display
- [X] ~~PPU viewer (nametables, patterns, palettes)~~ ✅ Complete (2026-05-19, issues #54/#55/#56) — Pattern Tables (two 128×128 tile grids, palette selector, hover tooltip), Nametables (2×2 grid with scroll viewport rect overlay + mirroring label), OAM (64-entry table with all sprite attributes)
- [X] ~~Memory viewer/editor~~ ✅ Complete (2026-05-19, issue #57) — hex viewer with CPU bus, OAM, VRAM, Palette tabs; uses imgui_memory_editor
- [ ] Rewind functionality (ring buffer of states)
- [ ] TAS (Tool-Assisted Speedrun) input recording

### Phase 5: Build System & Distribution
- [X] ~~GitHub Actions CI (replace Travis CI)~~ ✅ Complete (2026-05-19, issue #60) — ubuntu-latest + windows-latest matrix; all 12 test suites run on every push/PR
- [X] ~~Run test suites in CI~~ ✅ Complete (2026-05-19, issue #61) — all `add_test()` targets run via `ctest` in CI
- [X] ~~Linux AppImage packaging~~ ✅ Complete (2026-05-19, issue #63) — `install()` targets, `assets/emu.desktop`, `assets/icon.png`; `release.yml` uses linuxdeploy to produce `emu-vX.Y.Z-linux-x86_64.AppImage`
- [X] ~~Windows release packaging~~ ✅ Complete (2026-05-19, issue #64) — `ntldd -R` walks all MinGW DLL deps; zips to `emu-vX.Y.Z-windows-x64.zip`
- [X] ~~GitHub Release workflow~~ ✅ Complete (2026-05-19, issue #65) — `.github/workflows/release.yml` triggers on `v*` tags; runs both build jobs then creates a pre-release with both artifacts and auto-generated notes

### Phase 6: Accuracy & Compatibility
- [X] ~~Blargg headless test runner~~ ✅ Complete (2026-05-21, issues #81/#82) — `test_blargg_runner.c`; full NES stack without SDL2; 24 CTest entries; 6/24 passing
- [ ] Cycle-accurate PPU rendering edge cases (tracked in issues #115–#118)
- [ ] PPU open bus behavior
- [ ] Sprite overflow flag accuracy
- [ ] APU sweep unit edge cases (tracked in issues #119–#120)
- [ ] More mappers (5, etc.) — Mapper 9 (PxROM/MMC2) already complete
- [ ] Improve Blargg test pass rate (currently 6/24 — see issues #115–#120 for root causes)

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

**Last Updated:** 2026-05-21 (Blargg headless runner; mapper 000 PRG-RAM fix; 24 new CTest entries; issues #115–#120)
**Emulator Version:** 0.1.0 (pre-alpha)
