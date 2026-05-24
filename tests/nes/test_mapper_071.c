/*
 * test_mapper_071.c
 *
 * Unit tests for the Camerica/Codemasters (Mapper 071) implementation.
 *
 * Mapper 071: switchable 16KB PRG bank at $8000-$BFFF, fixed last bank at
 * $C000-$FFFF.  CHR is 8KB RAM (no banking).  Fire Hawk variant uses writes
 * to $9000-$9FFF to select single-screen mirroring (bit 4).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_071.h"
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

/* Build a cartridge with num_prg 16KB PRG banks and 8KB CHR-RAM.
 * PRG bank i is filled with byte (i + 1). */
static struct nes_cartridge *make_cart(uint8_t num_prg) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = 0; /* CHR-RAM */
    cart->hdr = hdr;
    cart->mapper_id = 71;

    size_t prg_len = (size_t)num_prg * 0x4000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    for (uint8_t i = 0; i < num_prg; i++)
        memset(cart->prg_rom + i * 0x4000, i + 1, 0x4000);

    cart->chr_rom = calloc(1, 0x2000);
    cart->chr_rom_len = 0x2000;
    cart->chr_ram_allocated = 1;

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg) {
    struct nes_cartridge *cart = make_cart(num_prg);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge    = cart;
    map.mapper_id    = 71;
    map.num_prg_rom  = num_prg;
    map.num_chr_rom  = 0;
    map.mirroring    = MIRROR_HORIZONTAL;
    map.cpu_read     = mapper_071_cpu_read;
    map.cpu_write    = mapper_071_cpu_write;
    map.ppu_read     = mapper_071_ppu_read;
    map.ppu_write    = mapper_071_ppu_write;
    mapper_071_init(&map);
    return &map;
}

/* test_power_up_state
 *
 * At power-up prg_bank is 0: $8000-$BFFF maps to bank 0, $C000-$FFFF maps
 * to the last bank. */
static void test_power_up_state(void) {
    printf("\n[test_power_up_state]\n");

    struct mapper *map = make_mapper(4);

    uint8_t lo = mapper_071_cpu_read(map, 0x8000);
    ASSERT(lo == 1, "power-up: $8000 reads from PRG bank 0 (filled with 1)");

    uint8_t hi = mapper_071_cpu_read(map, 0xC000);
    ASSERT(hi == 4, "power-up: $C000 reads from last PRG bank (filled with 4)");
}

/* test_prg_bank_switch
 *
 * Write bank 2 to any address $8000-$FFFF; $8000 should now map to bank 2.
 * The fixed bank at $C000 stays at the last bank. */
static void test_prg_bank_switch(void) {
    printf("\n[test_prg_bank_switch]\n");

    struct mapper *map = make_mapper(4);

    mapper_071_cpu_write(map, 0xC000, 2);

    uint8_t lo = mapper_071_cpu_read(map, 0x8000);
    ASSERT(lo == 3, "bank switch: $8000 reads from bank 2 (filled with 3)");

    uint8_t hi = mapper_071_cpu_read(map, 0xC000);
    ASSERT(hi == 4, "$C000 stays fixed at last bank after switch");
}

/* test_prg_bank_switch_low_nibble
 *
 * Only the low 4 bits of the write value are used. Writing 0x12 to a PRG
 * bank select register should select bank 2 (0x12 & 0x0F = 2). */
static void test_prg_bank_switch_low_nibble(void) {
    printf("\n[test_prg_bank_switch_low_nibble]\n");

    struct mapper *map = make_mapper(4);

    mapper_071_cpu_write(map, 0xFFFF, 0x12);

    uint8_t lo = mapper_071_cpu_read(map, 0x8000);
    ASSERT(lo == 3, "bank switch masks to low nibble: 0x12 -> bank 2 (filled with 3)");
}

/* test_prg_write_anywhere_in_range
 *
 * Writes to any address $8000-$FFFF (that is not $9000-$9FFF) should select
 * a PRG bank. */
static void test_prg_write_anywhere_in_range(void) {
    printf("\n[test_prg_write_anywhere_in_range]\n");

    struct mapper *map = make_mapper(4);

    mapper_071_cpu_write(map, 0xA000, 1);
    uint8_t v = mapper_071_cpu_read(map, 0x8000);
    ASSERT(v == 2, "write to $A000 selects bank 1 (filled with 2)");

    mapper_071_cpu_write(map, 0x8000, 3);
    v = mapper_071_cpu_read(map, 0x8000);
    ASSERT(v == 4, "write to $8000 selects bank 3 (filled with 4)");
}

/* test_chr_ram_read_write
 *
 * CHR-RAM: PPU writes should persist and be read back. */
static void test_chr_ram_read_write(void) {
    printf("\n[test_chr_ram_read_write]\n");

    struct mapper *map = make_mapper(2);

    mapper_071_ppu_write(map, 0x0100, 0xBE);
    uint8_t v = mapper_071_ppu_read(map, 0x0100);
    ASSERT(v == 0xBE, "CHR-RAM: write then read-back matches");

    mapper_071_ppu_write(map, 0x1FFF, 0xEF);
    v = mapper_071_ppu_read(map, 0x1FFF);
    ASSERT(v == 0xEF, "CHR-RAM: write to $1FFF read-back matches");
}

/* test_firehawk_mirroring_single_lo
 *
 * Fire Hawk variant: write to $9000-$9FFF with bit 4 = 0 selects
 * MIRROR_SINGLE_LO. */
static void test_firehawk_mirroring_single_lo(void) {
    printf("\n[test_firehawk_mirroring_single_lo]\n");

    struct mapper *map = make_mapper(4);

    /* bit 4 = 0 -> MIRROR_SINGLE_LO */
    mapper_071_cpu_write(map, 0x9000, 0x00);
    ASSERT(map->mirroring == MIRROR_SINGLE_LO,
           "Fire Hawk: write $00 to $9000 -> MIRROR_SINGLE_LO");

    mapper_071_cpu_write(map, 0x9FFF, 0x08);  /* bit 4 clear, other bits set */
    ASSERT(map->mirroring == MIRROR_SINGLE_LO,
           "Fire Hawk: write $08 to $9FFF -> MIRROR_SINGLE_LO (bit 4 = 0)");
}

/* test_firehawk_mirroring_single_hi
 *
 * Fire Hawk variant: write to $9000-$9FFF with bit 4 = 1 selects
 * MIRROR_SINGLE_HI. */
static void test_firehawk_mirroring_single_hi(void) {
    printf("\n[test_firehawk_mirroring_single_hi]\n");

    struct mapper *map = make_mapper(4);

    /* bit 4 = 1 -> MIRROR_SINGLE_HI */
    mapper_071_cpu_write(map, 0x9000, 0x10);
    ASSERT(map->mirroring == MIRROR_SINGLE_HI,
           "Fire Hawk: write $10 to $9000 -> MIRROR_SINGLE_HI");

    mapper_071_cpu_write(map, 0x9800, 0xFF);  /* all bits set, bit 4 set */
    ASSERT(map->mirroring == MIRROR_SINGLE_HI,
           "Fire Hawk: write $FF to $9800 -> MIRROR_SINGLE_HI (bit 4 = 1)");
}

/* test_firehawk_mirroring_does_not_affect_prg
 *
 * Writes to $9000-$9FFF update mirroring only, not the PRG bank register. */
static void test_firehawk_mirroring_does_not_affect_prg(void) {
    printf("\n[test_firehawk_mirroring_does_not_affect_prg]\n");

    struct mapper *map = make_mapper(4);

    /* Select bank 2 via a normal PRG write */
    mapper_071_cpu_write(map, 0xC000, 2);
    uint8_t before = mapper_071_cpu_read(map, 0x8000);
    ASSERT(before == 3, "PRG bank 2 selected before mirroring write");

    /* Mirroring write must not change the PRG bank */
    mapper_071_cpu_write(map, 0x9000, 0x10);
    uint8_t after = mapper_071_cpu_read(map, 0x8000);
    ASSERT(after == 3,
           "PRG bank unchanged after mirroring write to $9000");
    ASSERT(map->mirroring == MIRROR_SINGLE_HI,
           "Mirroring updated to SINGLE_HI independently of PRG bank");
}

/* test_two_bank_cart
 *
 * With a 2-bank cartridge: bank 0 at $8000, bank 1 (last) at $C000.
 * Switching to bank 1 at $8000 makes both windows read bank 1. */
static void test_two_bank_cart(void) {
    printf("\n[test_two_bank_cart]\n");

    struct mapper *map = make_mapper(2);

    uint8_t lo = mapper_071_cpu_read(map, 0x8000);
    ASSERT(lo == 1, "2-bank: $8000 default is bank 0 (filled with 1)");

    uint8_t hi = mapper_071_cpu_read(map, 0xC000);
    ASSERT(hi == 2, "2-bank: $C000 is last bank (filled with 2)");

    mapper_071_cpu_write(map, 0x8000, 1);
    lo = mapper_071_cpu_read(map, 0x8000);
    ASSERT(lo == 2, "2-bank: after switch $8000 = bank 1 (filled with 2)");
}

int main(void) {
    printf("=== Mapper 071 (Camerica/Codemasters) unit tests ===\n");

    test_power_up_state();
    test_prg_bank_switch();
    test_prg_bank_switch_low_nibble();
    test_prg_write_anywhere_in_range();
    test_chr_ram_read_write();
    test_firehawk_mirroring_single_lo();
    test_firehawk_mirroring_single_hi();
    test_firehawk_mirroring_does_not_affect_prg();
    test_two_bank_cart();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
