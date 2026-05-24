/*
 * test_mapper_007.c
 *
 * Unit tests for the AxROM (Mapper 007) implementation.
 *
 * AxROM: one switchable 32KB PRG bank at $8000-$FFFF, 8KB CHR-RAM,
 * single-screen mirroring selected by bit 4 of the bank register.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_007.h"
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

/* Build a cartridge with num_prg 32KB PRG banks and 8KB CHR-RAM.
 * PRG bank i is filled with byte (i + 1). */
static struct nes_cartridge *make_cart(uint8_t num_prg_32k) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    /* iNES prg_rom_size is in 16KB units; num_prg_32k 32KB banks = 2x that */
    hdr->prg_rom_size = num_prg_32k * 2;
    hdr->chr_rom_size = 0; /* CHR-RAM */
    cart->hdr = hdr;
    cart->mapper_id = 7;

    size_t prg_len = (size_t)num_prg_32k * 0x8000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    for (uint8_t i = 0; i < num_prg_32k; i++)
        memset(cart->prg_rom + (size_t)i * 0x8000, i + 1, 0x8000);

    cart->chr_rom = calloc(1, 0x2000);
    cart->chr_rom_len = 0x2000;
    cart->chr_ram_allocated = 1;

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg_32k) {
    struct nes_cartridge *cart = make_cart(num_prg_32k);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge    = cart;
    map.mapper_id    = 7;
    map.num_prg_rom  = num_prg_32k * 2; /* in 16KB units */
    map.num_chr_rom  = 0;
    map.cpu_read     = mapper_007_cpu_read;
    map.cpu_write    = mapper_007_cpu_write;
    map.ppu_read     = mapper_007_ppu_read;
    map.ppu_write    = mapper_007_ppu_write;
    mapper_007_init(&map);
    return &map;
}

/* test_power_up_state
 *
 * At power-up, bank register = 0: the entire $8000-$FFFF window maps to
 * PRG bank 0, and mirroring is MIRROR_SINGLE_LO. */
static void test_power_up_state(void) {
    printf("\n[test_power_up_state]\n");

    struct mapper *map = make_mapper(4);

    uint8_t lo = mapper_007_cpu_read(map, 0x8000);
    ASSERT(lo == 1, "power-up: $8000 reads from PRG 32KB bank 0 (filled with 1)");

    uint8_t hi = mapper_007_cpu_read(map, 0xFFFF);
    ASSERT(hi == 1, "power-up: $FFFF also in bank 0 (32KB window)");

    ASSERT(map->mirroring == MIRROR_SINGLE_LO, "power-up: mirroring is MIRROR_SINGLE_LO");
}

/* test_bank_switch
 *
 * Write bank 2 to $8000 — the entire $8000-$FFFF window switches to bank 2. */
static void test_bank_switch(void) {
    printf("\n[test_bank_switch]\n");

    struct mapper *map = make_mapper(4);

    mapper_007_cpu_write(map, 0x8000, 2);

    uint8_t lo = mapper_007_cpu_read(map, 0x8000);
    ASSERT(lo == 3, "bank switch: $8000 reads from 32KB bank 2 (filled with 3)");

    uint8_t hi = mapper_007_cpu_read(map, 0xFFFF);
    ASSERT(hi == 3, "bank switch: $FFFF also in the new bank 2");
}

/* test_prg_bank_mask
 *
 * Only bits 0-2 select the PRG bank. Writing 0x0F should select bank 7.
 * With a 4-bank cart (banks 0-3) bank 7 wraps — but the mask itself is tested
 * by confirming bits above 2 are stripped by (data & 0x07). */
static void test_prg_bank_mask(void) {
    printf("\n[test_prg_bank_mask]\n");

    struct mapper *map = make_mapper(4);

    /* Write 0x11: bit 4 = 1 (mirror hi), bits 0-2 = 1 (bank 1) */
    mapper_007_cpu_write(map, 0x8000, 0x11);

    uint8_t v = mapper_007_cpu_read(map, 0x8000);
    ASSERT(v == 2, "0x11 write: PRG bank bits 0-2 = 1 → bank 1 (filled with 2)");
}

/* test_write_anywhere_in_range
 *
 * Writes to any address $8000-$FFFF update the bank register. */
static void test_write_anywhere_in_range(void) {
    printf("\n[test_write_anywhere_in_range]\n");

    struct mapper *map = make_mapper(4);

    mapper_007_cpu_write(map, 0xC000, 1);
    uint8_t v = mapper_007_cpu_read(map, 0x8000);
    ASSERT(v == 2, "write to $C000 selects bank 1 (filled with 2)");

    mapper_007_cpu_write(map, 0xFFFF, 3);
    v = mapper_007_cpu_read(map, 0x8000);
    ASSERT(v == 4, "write to $FFFF selects bank 3 (filled with 4)");
}

/* test_mirroring_single_lo
 *
 * Bit 4 = 0 → MIRROR_SINGLE_LO. */
static void test_mirroring_single_lo(void) {
    printf("\n[test_mirroring_single_lo]\n");

    struct mapper *map = make_mapper(2);

    mapper_007_cpu_write(map, 0x8000, 0x00); /* bit 4 = 0 */
    ASSERT(map->mirroring == MIRROR_SINGLE_LO, "bit 4 = 0 → MIRROR_SINGLE_LO");
}

/* test_mirroring_single_hi
 *
 * Bit 4 = 1 → MIRROR_SINGLE_HI. */
static void test_mirroring_single_hi(void) {
    printf("\n[test_mirroring_single_hi]\n");

    struct mapper *map = make_mapper(2);

    mapper_007_cpu_write(map, 0x8000, 0x10); /* bit 4 = 1 */
    ASSERT(map->mirroring == MIRROR_SINGLE_HI, "bit 4 = 1 → MIRROR_SINGLE_HI");
}

/* test_chr_ram_read_write
 *
 * CHR-RAM: PPU writes persist and can be read back. */
static void test_chr_ram_read_write(void) {
    printf("\n[test_chr_ram_read_write]\n");

    struct mapper *map = make_mapper(2);

    mapper_007_ppu_write(map, 0x0200, 0xAB);
    uint8_t v = mapper_007_ppu_read(map, 0x0200);
    ASSERT(v == 0xAB, "CHR-RAM: write then read-back matches");

    mapper_007_ppu_write(map, 0x1FFF, 0x55);
    v = mapper_007_ppu_read(map, 0x1FFF);
    ASSERT(v == 0x55, "CHR-RAM: write at top of range read back correctly");
}

int main(void) {
    printf("=== Mapper 007 (AxROM) unit tests ===\n");

    test_power_up_state();
    test_bank_switch();
    test_prg_bank_mask();
    test_write_anywhere_in_range();
    test_mirroring_single_lo();
    test_mirroring_single_hi();
    test_chr_ram_read_write();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
