/*
 * test_mapper_069.c
 *
 * Unit tests for the Sunsoft FME-7 (Mapper 069) implementation.
 *
 * FME-7: up to 512KB PRG-ROM in four switchable 8KB windows ($8000-$FFFF).
 * CHR-ROM: up to 256KB in eight independently switchable 1KB windows.
 * PRG-RAM: 8KB at $6000-$7FFF (enabled via command $8 bit 6).
 * Mirroring: dynamically selectable (V/H/single-lo/single-hi) via command $D.
 * IRQ: 16-bit cycle-accurate down-counter; fires when it hits 0 with irq_enable=1.
 *
 * Compatible games: Gimmick!, Batman: Return of the Joker, Hebereke, Gremlins 2.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_069.h"
#include "cartridge.h"

static int test_pass = 0;
static int test_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (cond) {                                                    \
            printf("  PASS: %s\n", msg);                              \
            test_pass++;                                               \
        } else {                                                       \
            printf("  FAIL: %s  (line %d)\n", msg, __LINE__);        \
            test_fail++;                                               \
        }                                                              \
    } while (0)

/*
 * Build a cartridge with num_prg 16KB PRG banks (num_prg*2 = 8KB banks)
 * and num_chr 8KB CHR banks (num_chr*8 = 1KB banks).
 *
 * PRG 8KB bank i is filled with byte (i + 1).
 * CHR 1KB bank j is filled with byte (0x20 + j).
 */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = num_chr;
    cart->hdr = hdr;
    cart->mapper_id = 69;

    size_t prg_len = (size_t)num_prg * 0x4000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    uint16_t n8kb = num_prg * 2;
    for (uint16_t i = 0; i < n8kb; i++)
        memset(cart->prg_rom + i * 0x2000, (uint8_t)(i + 1), 0x2000);

    if (num_chr > 0) {
        size_t chr_len = (size_t)num_chr * 0x2000;
        cart->chr_rom = calloc(1, chr_len);
        cart->chr_rom_len = chr_len;
        cart->chr_ram_allocated = 0;
        uint16_t n1kb = num_chr * 8;
        for (uint16_t j = 0; j < n1kb; j++)
            memset(cart->chr_rom + j * 0x0400, (uint8_t)(0x20 + j), 0x0400);
    }

    return cart;
}

/* Build a mapper wired to the given cartridge. */
static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge   = cart;
    map.mapper_id   = 69;
    map.num_prg_rom = num_prg;
    map.num_chr_rom = num_chr;
    map.cpu_read    = mapper_069_cpu_read;
    map.cpu_write   = mapper_069_cpu_write;
    map.ppu_read    = mapper_069_ppu_read;
    map.ppu_write   = mapper_069_ppu_write;
    map.clock       = mapper_069_clock;
    mapper_069_init(&map);
    return &map;
}

/* Helper: write a command/parameter pair. */
static void cmd_param(struct mapper *map, uint8_t cmd, uint8_t param) {
    mapper_069_cpu_write(map, 0x8000, cmd);   /* command register */
    mapper_069_cpu_write(map, 0xA000, param); /* parameter register */
}

/* ---------------------------------------------------------------------------
 * test_power_up_prg
 *
 * At power-up prg_bank[3] is set to last 8KB bank; all other bank
 * registers default to 0.  With 4 16KB banks (8 8KB banks, n8kb=8):
 *   $8000 → bank 0 (filled 0x01)
 *   $A000 → bank 0 (filled 0x01)
 *   $C000 → bank 0 (filled 0x01)
 *   $E000 → bank 7 (filled 0x08)  ← init sets to last bank
 * -------------------------------------------------------------------------- */
static void test_power_up_prg(void) {
    printf("\n[test_power_up_prg]\n");

    struct mapper *map = make_mapper(4, 1);

    uint8_t v8 = mapper_069_cpu_read(map, 0x8000);
    ASSERT(v8 == 1, "power-up: $8000 → bank 0 (filled 0x01)");

    uint8_t ve = mapper_069_cpu_read(map, 0xE000);
    ASSERT(ve == 8, "power-up: $E000 → last bank 7 (filled 0x08)");
}

/* ---------------------------------------------------------------------------
 * test_prg_bank_switch
 *
 * cmd $9 selects $8000, $A → $A000, $B → $C000, $C → $E000.
 * With 4 16KB PRG (8 × 8KB banks):
 *   cmd $9, param=2 → $8000 bank 2 (filled 0x03)
 *   cmd $A, param=5 → $A000 bank 5 (filled 0x06)
 *   cmd $B, param=3 → $C000 bank 3 (filled 0x04)
 *   cmd $C, param=6 → $E000 bank 6 (filled 0x07)
 * -------------------------------------------------------------------------- */
static void test_prg_bank_switch(void) {
    printf("\n[test_prg_bank_switch]\n");

    struct mapper *map = make_mapper(4, 1);

    cmd_param(map, 0x09, 2);
    uint8_t v8 = mapper_069_cpu_read(map, 0x8000);
    ASSERT(v8 == 3, "cmd $9 param=2: $8000 = bank 2 (filled 0x03)");

    cmd_param(map, 0x0A, 5);
    uint8_t va = mapper_069_cpu_read(map, 0xA000);
    ASSERT(va == 6, "cmd $A param=5: $A000 = bank 5 (filled 0x06)");

    cmd_param(map, 0x0B, 3);
    uint8_t vc = mapper_069_cpu_read(map, 0xC000);
    ASSERT(vc == 4, "cmd $B param=3: $C000 = bank 3 (filled 0x04)");

    cmd_param(map, 0x0C, 6);
    uint8_t ve = mapper_069_cpu_read(map, 0xE000);
    ASSERT(ve == 7, "cmd $C param=6: $E000 = bank 6 (filled 0x07)");
}

/* ---------------------------------------------------------------------------
 * test_chr_bank_switch
 *
 * Eight 1KB CHR windows, each selected by cmd $0-$7.
 * With 2 8KB CHR banks = 16 × 1KB banks (filled 0x20-0x2F):
 *   cmd $0, param=3  → PPU $0000 reads bank 3 (filled 0x23)
 *   cmd $4, param=10 → PPU $1000 reads bank 10 (filled 0x2A)
 *   cmd $7, param=15 → PPU $1C00 reads bank 15 (filled 0x2F)
 * -------------------------------------------------------------------------- */
static void test_chr_bank_switch(void) {
    printf("\n[test_chr_bank_switch]\n");

    struct mapper *map = make_mapper(2, 2);

    cmd_param(map, 0x00, 3);
    uint8_t v0 = mapper_069_ppu_read(map, 0x0000);
    ASSERT(v0 == 0x23, "cmd $0 param=3: PPU $0000 = bank 3 (0x23)");

    cmd_param(map, 0x04, 10);
    uint8_t v4 = mapper_069_ppu_read(map, 0x1000);
    ASSERT(v4 == 0x2A, "cmd $4 param=10: PPU $1000 = bank 10 (0x2A)");

    cmd_param(map, 0x07, 15);
    uint8_t v7 = mapper_069_ppu_read(map, 0x1C00);
    ASSERT(v7 == 0x2F, "cmd $7 param=15: PPU $1C00 = bank 15 (0x2F)");
}

/* ---------------------------------------------------------------------------
 * test_mirroring
 *
 * cmd $D, bits 1-0: 00=V, 01=H, 10=single-lo, 11=single-hi.
 * -------------------------------------------------------------------------- */
static void test_mirroring(void) {
    printf("\n[test_mirroring]\n");

    struct mapper *map = make_mapper(2, 1);

    cmd_param(map, 0x0D, 0); /* vertical */
    ASSERT(map->mirroring == MIRROR_VERTICAL,   "cmd $D 0: vertical");

    cmd_param(map, 0x0D, 1); /* horizontal */
    ASSERT(map->mirroring == MIRROR_HORIZONTAL, "cmd $D 1: horizontal");

    cmd_param(map, 0x0D, 2); /* single-screen low */
    ASSERT(map->mirroring == MIRROR_SINGLE_LO,  "cmd $D 2: single-lo");

    cmd_param(map, 0x0D, 3); /* single-screen high */
    ASSERT(map->mirroring == MIRROR_SINGLE_HI,  "cmd $D 3: single-hi");
}

/* ---------------------------------------------------------------------------
 * test_prg_ram
 *
 * cmd $8 bit 6 = RAM enable, bit 7 = ROM select (0 = use internal RAM).
 * Write 0x40 to cmd $8 → enable RAM, not ROM.
 * Write then read-back from $6100.
 * -------------------------------------------------------------------------- */
static void test_prg_ram(void) {
    printf("\n[test_prg_ram]\n");

    struct mapper *map = make_mapper(2, 1);

    /* Enable PRG-RAM (bit 6 = 1, bit 7 = 0 → internal RAM). */
    cmd_param(map, 0x08, 0x40);

    mapper_069_cpu_write(map, 0x6100, 0xAB);
    uint8_t v = mapper_069_cpu_read(map, 0x6100);
    ASSERT(v == 0xAB, "PRG-RAM enabled: write then read-back matches");

    /* Disable RAM → reads return 0xFF. */
    cmd_param(map, 0x08, 0x00); /* bit 6 = 0 → disabled */
    uint8_t vd = mapper_069_cpu_read(map, 0x6100);
    ASSERT(vd == 0xFF, "PRG-RAM disabled: reads return 0xFF");
}

/* ---------------------------------------------------------------------------
 * test_irq_counter_fires
 *
 * Load a counter value of 3, enable counter and IRQ.
 * Clock 3 times; IRQ should fire after the third clock (counter wraps to 0).
 * -------------------------------------------------------------------------- */
static void test_irq_counter_fires(void) {
    printf("\n[test_irq_counter_fires]\n");

    struct mapper *map = make_mapper(2, 1);

    /* cmd $F = low byte of counter; cmd $E = control bits.
     * Set counter to 3 first, then enable. */
    cmd_param(map, 0x0F, 3);             /* low byte = 3   */
    cmd_param(map, 0x0E, 0x81);          /* irq_enable=1, counter_enable=1 */

    ASSERT(map->irq_pending == 0, "IRQ: not pending before any clocks");

    mapper_069_clock(map); /* counter: 3 → 2 */
    ASSERT(map->irq_pending == 0, "IRQ: not pending after 1st clock (counter=2)");

    mapper_069_clock(map); /* counter: 2 → 1 */
    ASSERT(map->irq_pending == 0, "IRQ: not pending after 2nd clock (counter=1)");

    mapper_069_clock(map); /* counter: 1 → 0, fires */
    ASSERT(map->irq_pending == 1, "IRQ: pending after 3rd clock (counter=0)");
}

/* ---------------------------------------------------------------------------
 * test_irq_counter_enable
 *
 * With counter_enable=0, the counter should not decrement.
 * -------------------------------------------------------------------------- */
static void test_irq_counter_enable(void) {
    printf("\n[test_irq_counter_enable]\n");

    struct mapper *map = make_mapper(2, 1);

    cmd_param(map, 0x0F, 1);    /* counter low = 1 */
    cmd_param(map, 0x0E, 0x01); /* irq_enable=1, counter_enable=0 (bit 7 = 0) */

    mapper_069_clock(map);
    mapper_069_clock(map);
    ASSERT(map->irq_pending == 0,
           "IRQ: counter_enable=0 — counter does not decrement, no IRQ");
}

/* ---------------------------------------------------------------------------
 * test_irq_cleared_by_cmd_e_write
 *
 * Writing to cmd $E clears the IRQ pending flag.
 * -------------------------------------------------------------------------- */
static void test_irq_cleared_by_cmd_e_write(void) {
    printf("\n[test_irq_cleared_by_cmd_e_write]\n");

    struct mapper *map = make_mapper(2, 1);

    cmd_param(map, 0x0F, 1);     /* counter = 1 */
    cmd_param(map, 0x0E, 0x81);  /* enable counter + IRQ */

    mapper_069_clock(map); /* fires: counter goes 1 → 0 */
    ASSERT(map->irq_pending == 1, "IRQ: fires after counter hits 0");

    /* Write cmd $E again to clear IRQ pending (disable counter). */
    cmd_param(map, 0x0E, 0x00);
    ASSERT(map->irq_pending == 0, "IRQ: cleared by cmd $E write");
}

/* ---------------------------------------------------------------------------
 * test_prg_bank_masking
 *
 * PRG bank register is masked to 6 bits (0x3F).  Writing a value > 63
 * should be masked, selecting the correct bank modulo num_8kb.
 * With 4 16KB PRG banks (8 × 8KB), num_8kb=8.
 * Write 0xFF to cmd $9 → 0xFF & 0x3F = 63, 63 % 8 = 7 (bank 7, filled 0x08).
 * -------------------------------------------------------------------------- */
static void test_prg_bank_masking(void) {
    printf("\n[test_prg_bank_masking]\n");

    struct mapper *map = make_mapper(4, 1);

    cmd_param(map, 0x09, 0xFF); /* bank = 0xFF & 0x3F = 63, 63 % 8 = 7 */
    uint8_t v = mapper_069_cpu_read(map, 0x8000);
    ASSERT(v == 8, "PRG bank masked: 0xFF writes select bank 7 (filled 0x08)");
}

/* ---------------------------------------------------------------------------
 * test_chr_independent_windows
 *
 * All 8 CHR windows are independently addressable without aliasing.
 * Set each window to a distinct bank, verify each reads the right data.
 * 2 8KB CHR banks = 16 × 1KB banks.
 * -------------------------------------------------------------------------- */
static void test_chr_independent_windows(void) {
    printf("\n[test_chr_independent_windows]\n");

    struct mapper *map = make_mapper(2, 2);

    /* Set each of the 8 windows to the corresponding 1KB bank. */
    for (uint8_t w = 0; w < 8; w++)
        cmd_param(map, w, w); /* window w → CHR bank w (filled 0x20+w) */

    int ok = 1;
    for (uint8_t w = 0; w < 8; w++) {
        uint16_t ppu_addr = (uint16_t)w * 0x0400;
        uint8_t  expected = (uint8_t)(0x20 + w);
        uint8_t  got      = mapper_069_ppu_read(map, ppu_addr);
        if (got != expected) {
            printf("  FAIL: window %d: expected 0x%02X, got 0x%02X (line %d)\n",
                   w, expected, got, __LINE__);
            ok = 0;
        }
    }
    if (ok) printf("  PASS: all 8 CHR windows independently addressable\n");
    test_pass += ok;
    test_fail += !ok;
}

int main(void) {
    printf("=== Mapper 069 (Sunsoft FME-7) unit tests ===\n");

    test_power_up_prg();
    test_prg_bank_switch();
    test_chr_bank_switch();
    test_mirroring();
    test_prg_ram();
    test_irq_counter_fires();
    test_irq_counter_enable();
    test_irq_cleared_by_cmd_e_write();
    test_prg_bank_masking();
    test_chr_independent_windows();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
