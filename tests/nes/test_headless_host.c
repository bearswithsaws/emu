/**
 * test_headless_host.c — lib_nes embedding smoke test (issue #158)
 *
 * Loads nestest.nes, runs 60 frames with no frame buffer and no audio
 * device, then asserts a clean exit.
 *
 * IMPORTANT: this file must NOT include any SDL2 headers.
 * If it compiles and links with only lib_nes (no libdisplay, no libinput,
 * no libui, no SDL2 on the link line) the decoupling is verified.
 *
 * Build target: test_headless_host  (see tests/nes/CMakeLists.txt)
 * Link:  lib_nes only  — SDL2 absence on the link line is the key check.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "bus.h"
#include "ppu.h"
#include "apu.h"
#include "6502.h"
#include "input.h"

#ifndef NESTEST_ROM_PATH
#error "NESTEST_ROM_PATH must be defined by CMake"
#endif

/* Simple assertion helper */
#define ASSERT(cond, msg)                                           \
    do {                                                            \
        if (!(cond)) {                                              \
            fprintf(stderr, "FAIL [%s:%d]: %s\n",                  \
                    __FILE__, __LINE__, msg);                       \
            return 1;                                               \
        }                                                           \
    } while (0)

/* Run one NES tick (mirrors emu_tick() in emu.c). */
static void emu_tick(struct nesbus *bus, struct cpu6502 *cpu,
                     struct ppu2c02 *ppu) {
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

int main(void) {
    printf("test_headless_host: loading %s\n", NESTEST_ROM_PATH);

    /* --- Initialise NES core (no SDL2) --- */
    struct nes_cartridge *cart = load_rom(NESTEST_ROM_PATH);
    ASSERT(cart != NULL, "load_rom() returned NULL");

    struct cpu6502 *cpu = cpu6502_init();
    struct ppu2c02 *ppu = ppu2c02_init();
    struct nesbus  *bus = nesbus_init(cpu, ppu);
    ASSERT(bus != NULL, "nesbus_init() returned NULL");

    bus->connect_cartridge(cart);
    ppu->connect_cartridge(cart);

    /* Headless: no frame buffer — PPU clocks correctly, no pixel writes. */
    ppu->set_framebuffer(NULL);

    cpu->reset();
    apu_reset(bus->apu);

    /* --- Run 60 frames --- */
    float audio_buf[1024];
    int total_audio_samples = 0;

    for (int frame = 0; frame < 60; frame++) {
        /* Both controllers held neutral. */
        nes_set_buttons(bus, 1, 0x00);
        nes_set_buttons(bus, 2, 0x00);

        ppu->frame_complete = 0;
        while (!ppu->frame_complete)
            emu_tick(bus, cpu, ppu);

        /* Drain audio — must not crash or return negative. */
        int got = apu_drain_samples(bus->apu, audio_buf, 1024);
        ASSERT(got >= 0, "apu_drain_samples() returned negative");
        ASSERT(got <= 1024, "apu_drain_samples() returned more than max");
        total_audio_samples += got;
    }

    /* Sanity: 60 frames at ~735 samples/frame → expect > 30000 total */
    ASSERT(total_audio_samples > 30000,
           "unexpectedly few audio samples produced");

    printf("PASS — 60 frames, PC=0x%04X, audio=%d samples\n",
           cpu->PC, total_audio_samples);
    return 0;
}
