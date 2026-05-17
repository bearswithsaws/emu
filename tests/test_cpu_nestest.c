/*
 * test_cpu_nestest.c
 *
 * CPU integration test using the nestest.nes ROM — the gold-standard NES CPU
 * accuracy test by Kevin Horton.
 *
 * Automation mode: set PC = $C000 (instead of the reset vector $C004 which
 * launches the interactive menu).  The ROM runs all official and illegal
 * opcode tests, accumulating results in zero-page registers:
 *   $00 = last failed test (0 = none)
 *   $10 = result of official opcode tests  (0 = pass)
 *   $11 = result of illegal opcode tests   (0 = pass)
 *
 * Completion is detected when PC enters the zero-page range ($0000-$00FF)
 * after the ROM has run all test subroutines.  The combined result is
 * mem[$00] | mem[$10] | mem[$11]; 0 means all tests passed.
 *
 * The ROM path is injected at compile time via -DNESTEST_ROM_PATH="..." in
 * tests/CMakeLists.txt.
 *
 * Architecture:
 *   - Flat 64 KB memory array (no SDL2, no PPU, no APU).
 *   - Minimal nesbus struct with read/write/debug_read backed by the array.
 *   - PRG-ROM (16 KB) loaded at $8000 and mirrored to $C000.
 *   - cpu->reset() sets up SP/flags; PC is then forced to $C000.
 *   - Clock loop runs until PC enters zero page (or cycle limit).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "6502.h"
#include "nesbus.h"

/* -------------------------------------------------------------------------
 * Flat 64 KB test address space
 * ------------------------------------------------------------------------- */

static uint8_t mem[65536];

static uint8_t  test_read(uint16_t addr)                        { return mem[addr]; }
static int g_batch10_init = 0; /* skip first (init) write to $10/$11 */
static int g_batch11_init = 0;

static void     test_write(uint16_t addr, uint8_t data) {
    /* Show progress when batch result registers are committed (skip init write) */
    if (addr == 0x0010) {
        if (g_batch10_init++)
            printf("  [official batch] result=$%02X %s\n", data,
                   data == 0 ? "(PASS)" : "(FAIL)");
    } else if (addr == 0x0011) {
        if (g_batch11_init++)
            printf("  [illegal batch]  result=$%02X %s\n", data,
                   data == 0 ? "(PASS)" : "(FAIL)");
    }
    mem[addr] = data;
}
static uint8_t *test_debug_read(uint16_t off, uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++)
        buf[i] = mem[(uint16_t)(off + i)];
    return buf;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (cond) {                                                    \
            printf("  PASS: %s\n", msg);                              \
            g_pass++;                                                  \
        } else {                                                       \
            printf("  FAIL: %s\n", msg);                              \
            g_fail++;                                                  \
        }                                                              \
    } while (0)

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void) {
    printf("=== nestest CPU integration test ===\n");

    /* Load ROM ------------------------------------------------------------ */
    FILE *f = fopen(NESTEST_ROM_PATH, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open ROM: %s\n", NESTEST_ROM_PATH);
        return 1;
    }
    fseek(f, 16, SEEK_SET);                    /* skip 16-byte iNES header */
    size_t n = fread(mem + 0x8000, 1, 0x4000, f);
    fclose(f);
    if (n != 0x4000) {
        fprintf(stderr, "Short read: expected 16384 bytes, got %zu\n", n);
        return 1;
    }
    memcpy(mem + 0xC000, mem + 0x8000, 0x4000); /* mirror to $C000-$FFFF */

    /* Minimal bus --------------------------------------------------------- */
    static struct nesbus testbus;
    memset(&testbus, 0, sizeof(testbus));
    testbus.read        = test_read;
    testbus.write       = test_write;
    testbus.debug_read  = test_debug_read;
    /* ppu, cart, apu all NULL — guarded by null-checks in 6502.c clock()  */

    /* CPU init ------------------------------------------------------------ */
    struct cpu6502 *cpu = cpu6502_init();
    cpu->connect_bus(&testbus);
    cpu->reset();            /* sets up SP=$FD, flags; reads reset vector    */
    cpu->PC = 0xC000;        /* override to automation-mode entry point      */

    /* Run until PC enters zero page (test completion) or cycle limit ------ */
    const uint64_t MAX_CYCLES = 5000000ULL;
    uint64_t cycles = 0;

    while (cycles < MAX_CYCLES) {
        if (cpu->cycles == 0 && cpu->PC < 0x0100)
            break;
        cpu->clock();
        cycles++;
    }

    printf("\n[test_nestest_official_opcodes]\n");

    if (cycles >= MAX_CYCLES) {
        printf("  FAIL: test did not complete after %llu cycles (timeout)\n",
               (unsigned long long)cycles);
        g_fail++;
    } else {
        /* Result registers: $00 = last failure, $10 = official, $11 = illegal */
        uint8_t result = mem[0x00] | mem[0x10] | mem[0x11];
        if (result == 0x00) {
            printf("  PASS: All opcode tests passed ($00=$10=$11=0)"
                   " after %llu cycles\n", (unsigned long long)cycles);
            g_pass++;
        } else {
            printf("  FAIL: Tests failed — $00=$%02X $10=$%02X $11=$%02X"
                   " after %llu cycles\n",
                   mem[0x00], mem[0x10], mem[0x11],
                   (unsigned long long)cycles);
            g_fail++;
        }
    }

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
