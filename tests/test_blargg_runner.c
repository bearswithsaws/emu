/*
 * test_blargg_runner.c
 *
 * Generic headless runner for Blargg-format NES test ROMs.
 *
 * All Blargg test ROMs (and many compatible test ROMs) communicate results
 * through a standard memory-mapped protocol:
 *
 *   $6001-$6003  Magic bytes $DE $B0 $61 — confirm the protocol is active
 *   $6000        Status: $80 = still running, $00 = pass, $01-$7F = fail code
 *   $6004+       Null-terminated text output (human-readable description)
 *
 * The full NES stack (CPU + PPU + APU + cartridge) runs without SDL2.
 * The PPU frame buffer pointer is left NULL; the guard in 2c02.c skips
 * pixel writes, so the PPU still tracks timing, NMI, sprite-0 hit, etc.
 * The APU ring buffer fills and wraps silently (no audio thread present).
 *
 * Usage:  test_blargg_runner <path/to/test.nes>
 * Exit:   0 = pass, 1 = fail or timeout
 *
 * Registers as CTest entries via tests/CMakeLists.txt when
 * -DBLARGG_TEST_ROMS_PATH=<dir> is supplied to cmake.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "2a03.h"
#include "2c02.h"
#include "6502.h"
#include "cartridge.h"
#include "nesbus.h"

/* Timeout: 600 frames ≈ 10 seconds of NES time at 60.1 fps. Most Blargg
 * tests complete in under 5 seconds; the slowest APU timing tests need ~8. */
#define TIMEOUT_FRAMES 600

/* Some tests (apu_reset suite) report $81 to request a CPU reset, then
 * expect execution to continue.  We honour this after a brief delay. */
#define RESET_REQUEST_STATUS 0x81
#define RESET_DELAY_FRAMES   6

#define BLARGG_STATUS_ADDR 0x6000
#define BLARGG_MAGIC_ADDR  0x6001
#define BLARGG_TEXT_ADDR   0x6004

static struct nesbus  *bus;
static struct cpu6502 *cpu;
static struct ppu2c02 *ppu;

static void emu_tick(void) {
    ppu->clock();
    ppu->clock();
    ppu->clock();
    if (bus->dma_halt_cycles > 0) {
        bus->dma_halt_cycles--;
    } else {
        cpu->clock();
    }
    apu_clock(bus->apu);
    if (apu_irq_pending(bus->apu))
        cpu->irq();
}

/* Returns result code (0=pass, 1+=fail), RESET_REQUEST_STATUS, or -1 if
 * still running / protocol not yet active. */
static int check_result(void) {
    if (bus->read(BLARGG_MAGIC_ADDR)     != 0xDE ||
        bus->read(BLARGG_MAGIC_ADDR + 1) != 0xB0 ||
        bus->read(BLARGG_MAGIC_ADDR + 2) != 0x61)
        return -1;
    uint8_t status = bus->read(BLARGG_STATUS_ADDR);
    return (status == 0x80) ? -1 : (int)status;
}

static void print_output(void) {
    int any = 0;
    for (uint16_t addr = BLARGG_TEXT_ADDR; ; addr++) {
        uint8_t c = bus->read(addr);
        if (!c) break;
        if (!any) { printf("  "); any = 1; }
        putchar(c);
        if (c == '\n') printf("  ");
    }
    if (any) putchar('\n');
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test.nes>\n", argv[0]);
        return 1;
    }

    const char *rom_path = argv[1];

    /* Strip path prefix for a cleaner test name in output. */
    const char *name = strrchr(rom_path, '/');
    name = name ? name + 1 : rom_path;
    printf("=== %s ===\n", name);

    struct nes_cartridge *cart = load_rom(rom_path);
    if (!cart) {
        fprintf(stderr, "FAIL: cannot load ROM: %s\n", rom_path);
        return 1;
    }

    cpu = cpu6502_init();
    ppu = ppu2c02_init();
    bus = nesbus_init(cpu, ppu);
    /* ppu->set_framebuffer() intentionally not called — NULL framebuffer is
     * guarded in 2c02.c so the PPU still tracks timing without rendering. */

    bus->connect_cartridge(cart);
    ppu->connect_cartridge(cart);
    cpu->reset();
    apu_reset(bus->apu);  /* simulate power-on $4017=$00 write (3-cycle delay) */

    int result      = -1;
    int frame       = 0;
    int reset_delay = 0; /* frames remaining after a $81 reset request */

    for (frame = 0; frame < TIMEOUT_FRAMES; frame++) {
        ppu->frame_complete = 0;
        while (!ppu->frame_complete)
            emu_tick();

        result = check_result();
        if (result < 0)
            continue;

        if (result == RESET_REQUEST_STATUS) {
            /* Test is asking for a CPU reset (e.g. apu_reset suite). */
            reset_delay++;
            if (reset_delay >= RESET_DELAY_FRAMES) {
                cpu->reset();
                apu_reset(bus->apu); /* APU resets with the CPU on real hardware */
                reset_delay = 0;
                result = -1; /* wait for the test to run again after reset */
            } else {
                result = -1; /* still counting down */
            }
            continue;
        }

        break; /* got a real result */
    }

    print_output();

    if (result < 0) {
        printf("  FAIL: timed out after %d frames\n", TIMEOUT_FRAMES);
        cartridge_free(cart);
        return 1;
    }

    if (result == 0)
        printf("  PASS (completed in %d frames)\n", frame + 1);
    else
        printf("  FAIL: result code $%02X (frame %d)\n", result, frame + 1);

    cartridge_free(cart);
    return (result == 0) ? 0 : 1;
}
