# Embedding lib_nes — Integration Guide

`lib_nes` is a self-contained NES emulation core with **no SDL2 dependency**.
It can be embedded into any host application: an SDL2 frontend, a native OS
UI, a web backend (Emscripten), a headless test harness, or an AI training
environment.

---

## What lib_nes provides

| Component | File(s) | Description |
|-----------|---------|-------------|
| 6502 CPU | `cpu/6502/6502.c` | Cycle-accurate MOS 6502 (via `lib_cpu_6502`) |
| PPU (2C02) | `ppu.c` | Full scanline renderer; writes into a host-supplied frame buffer |
| APU (2A03) | `apu.c` | All 5 channels; writes samples into a ring buffer the host drains |
| Bus | `bus.c` | Memory map, DMA, open-bus tracking |
| Cartridge | `cartridge.c` | iNES 1.0 / 2.0 loader |
| Mappers | `mappers/` | 13 mappers (000–071) |
| Controller | `controller.c` | Shift-register read/write; host writes button bitmask |
| Save state | `savestate.c` | Full state snapshot to file or memory buffer |
| Rewind | `rewind.c` | Ring buffer of snapshots for frame-level rewind |
| SRAM | `sram.c` | Battery-backed PRG-RAM persistence |
| TAS | `tas.c` | Input recording and deterministic playback |

## What the host must provide

- A **frame buffer** (256 × 240 × 4 bytes, ARGB8888) — or `NULL` for headless.
- An **audio sink** that drains `~735 float32 mono samples @ 44100 Hz` per frame.
- A **button bitmask** for each controller, set once per frame.

---

## CMake integration

```cmake
# In your project's CMakeLists.txt:
add_subdirectory(path/to/emu/cpu)       # defines lib_cpu_6502
add_subdirectory(path/to/emu/consoles)  # defines lib_nes (links lib_cpu_6502)

target_link_libraries(my_frontend PRIVATE lib_nes)

target_include_directories(my_frontend PRIVATE
    path/to/emu/consoles/nes
    path/to/emu/consoles/nes/mappers
    path/to/emu/cpu/6502
)
```

`lib_nes` links only `lib_cpu_6502`.  It does **not** link SDL2, OpenGL, or
any other platform library.  Your frontend links whatever it needs separately.

---

## Initialisation

```c
#include "cartridge.h"
#include "bus.h"
#include "ppu.h"
#include "apu.h"
#include "6502.h"
#include "input.h"

/* 1. Load the ROM. */
struct nes_cartridge *cart = load_rom("game.nes");
if (!cart) { /* handle error */ }

/* 2. Create the CPU, PPU, and bus. */
struct cpu6502  *cpu = cpu6502_init();
struct ppu2c02  *ppu = ppu2c02_init();
struct nesbus   *bus = nesbus_init(cpu, ppu);

/* 3. Attach the cartridge. */
bus->connect_cartridge(cart);
ppu->connect_cartridge(cart);

/* 4. Provide a frame buffer (256*240 ARGB8888).
 *    Pass NULL for headless operation — PPU still clocks correctly. */
uint32_t pixels[256 * 240];
ppu->set_framebuffer(pixels);           /* or NULL */

/* 5. Reset the CPU (loads reset vector, initialises APU). */
cpu->reset();
apu_reset(bus->apu);
```

---

## Per-frame loop

```c
while (running) {
    /* --- 1. Push controller state (host translates its input to bitmask) --- */
    nes_set_buttons(bus, 1, p1_buttons);   /* uint8_t NES_BTN_* OR-mask */
    nes_set_buttons(bus, 2, p2_buttons);

    /* --- 2. Run one NES frame --- */
    ppu->frame_complete = 0;
    while (!ppu->frame_complete) {
        /* 3 PPU dots per CPU cycle */
        ppu->clock(); ppu->clock(); ppu->clock();
        if (bus->dma_halt_cycles > 0)
            bus->dma_halt_cycles--;
        else
            cpu->clock();
        apu_clock(bus->apu);
        if (apu_irq_pending(bus->apu)) cpu->irq();
        if (bus->cart && bus->cart->map && bus->cart->map->clock)
            bus->cart->map->clock(bus->cart->map);
        bus->total_cycles++;
    }

    /* --- 3. Consume video (if frame buffer was non-NULL) --- */
    /* pixels[] now contains the completed 256×240 ARGB8888 frame.
     * Upload to a GPU texture, write to a file, or ignore for headless. */

    /* --- 4. Consume audio --- */
    float samples[1024];
    int count = apu_drain_samples(bus->apu, samples, 1024);
    /* Pass samples[0..count-1] to your audio API. */
}
```

---

## Controller button bitmask reference

| Bit | Constant | NES button |
|-----|----------|-----------|
| 0 | `NES_BTN_A` | A |
| 1 | `NES_BTN_B` | B |
| 2 | `NES_BTN_SELECT` | Select |
| 3 | `NES_BTN_START` | Start |
| 4 | `NES_BTN_UP` | D-pad Up |
| 5 | `NES_BTN_DOWN` | D-pad Down |
| 6 | `NES_BTN_LEFT` | D-pad Left |
| 7 | `NES_BTN_RIGHT` | D-pad Right |

Example — build a bitmask from SDL2 (host-layer code):
```c
const uint8_t *k = SDL_GetKeyboardState(NULL);
uint8_t p1 = ((k[SDL_SCANCODE_UP])    ? NES_BTN_UP    : 0) |
             ((k[SDL_SCANCODE_DOWN])  ? NES_BTN_DOWN  : 0) |
             ((k[SDL_SCANCODE_Z])     ? NES_BTN_A     : 0) |
             ((k[SDL_SCANCODE_X])     ? NES_BTN_B     : 0) /* … */;
nes_set_buttons(bus, 1, p1);
```

---

## Headless / no-display operation

Set `ppu->set_framebuffer(NULL)` — the PPU still clocks correctly (NMI fires,
sprite-0 hit is detected, timing is accurate).  Audio samples are still
produced; call `apu_drain_samples` to prevent the ring from filling, or simply
ignore them.

The Blargg headless test runner (`tests/nes/test_blargg_runner.c`) is a full
working example of SDL2-free NES operation.

---

## Save states and rewind

```c
/* Save to a slot file (slot 1–5): */
savestate_save(1, bus);
savestate_load(1, bus);

/* Save to / restore from an in-memory buffer (e.g. for rewind): */
size_t size;
uint8_t *snap = savestate_save_mem(bus, &size);   /* caller must free() */
savestate_load_mem(snap, size, bus);
free(snap);

/* Rewind ring buffer (30 s at 60 fps): */
struct rewind_buffer *rw = rewind_init();
rewind_push(rw, bus);   /* call once per frame */
rewind_step(rw, bus);   /* call to step back one frame */
rewind_free(rw);
```

---

## Audio notes

- The APU writes samples into a lock-free SPSC ring buffer at the CPU clock rate
  downsampled to 44100 Hz.
- Call `apu_drain_samples()` from your main thread (or audio thread — it is
  thread-safe with itself, but do not call it from two threads simultaneously).
- Drain at least once per frame to avoid the ring overflowing (~4 096 slots).
- If using SDL2's callback-mode audio, implement the callback in your host:

```c
/* Example SDL2 audio callback (host code only — not in lib_nes): */
static void my_audio_callback(void *userdata, uint8_t *stream, int len) {
    struct apu2a03 *apu = userdata;
    float *out = (float *)(void *)stream;
    int n   = len / sizeof(float);
    int got = apu_drain_samples(apu, out, n);
    memset(out + got, 0, (n - got) * sizeof(float)); /* silence on underrun */
}
```

---

## Thread safety

`lib_nes` is single-threaded.  All CPU/PPU/APU calls must come from the same
thread.  The only exception is `apu_drain_samples()`, which is safe to call
from a dedicated audio callback thread while the main thread writes to the
ring — this is the intended SPSC pattern.

---

## Minimal working example

The following (~40 lines) loads a ROM, runs 60 frames headlessly, then exits.
It has **no SDL2 dependency**.

```c
/* headless_example.c — compile with:
 *   cc headless_example.c -Icpu/6502 -Iconsoles/nes -Iconsoles/nes/mappers \
 *      -Lbuild -l_nes -l_cpu_6502 -o headless_example
 */
#include <stdio.h>
#include <stdlib.h>
#include "cartridge.h"
#include "bus.h"
#include "ppu.h"
#include "apu.h"
#include "6502.h"
#include "input.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s game.nes\n", argv[0]); return 1; }

    struct nes_cartridge *cart = load_rom(argv[1]);
    if (!cart) { fprintf(stderr, "failed to load %s\n", argv[1]); return 1; }

    struct cpu6502 *cpu = cpu6502_init();
    struct ppu2c02 *ppu = ppu2c02_init();
    struct nesbus  *bus = nesbus_init(cpu, ppu);
    bus->connect_cartridge(cart);
    ppu->connect_cartridge(cart);
    ppu->set_framebuffer(NULL);  /* headless — no pixel output */
    cpu->reset();
    apu_reset(bus->apu);

    float audio_buf[1024];

    for (int frame = 0; frame < 60; frame++) {
        nes_set_buttons(bus, 1, 0x00);
        nes_set_buttons(bus, 2, 0x00);

        ppu->frame_complete = 0;
        while (!ppu->frame_complete) {
            ppu->clock(); ppu->clock(); ppu->clock();
            if (bus->dma_halt_cycles > 0) bus->dma_halt_cycles--;
            else cpu->clock();
            apu_clock(bus->apu);
            if (apu_irq_pending(bus->apu)) cpu->irq();
            if (bus->cart && bus->cart->map && bus->cart->map->clock)
                bus->cart->map->clock(bus->cart->map);
            bus->total_cycles++;
        }

        /* Drain audio (discard — no speaker in this example). */
        apu_drain_samples(bus->apu, audio_buf, 1024);
    }

    printf("OK — 60 frames, PC=0x%04X\n", cpu->PC);
    return 0;
}
```
