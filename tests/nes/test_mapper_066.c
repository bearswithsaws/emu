/*
 * test_mapper_066.c
 *
 * Unit tests for the GxROM (Mapper 066) implementation.
 *
 * GxROM: one switchable 32KB PRG bank (bits 4-5) and one switchable 8KB CHR
 * bank (bits 0-1), both selected by a single write to $8000-$FFFF.
 * Uses CHR-ROM (not CHR-RAM) — PPU writes are no-ops.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_066.h"
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

/* Build a cartridge with num_prg 32KB PRG banks and num_chr 8KB CHR banks.
 * PRG bank i filled with (i + 1); CHR bank i filled with (0x10 + i). */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg * 2;
    hdr->chr_rom_size = num_chr;
    cart->hdr = hdr;
    cart->mapper_id = 66;

    size_t prg_len = (size_t)num_prg * 0x8000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    for (uint8_t i = 0; i < num_prg; i++)
        memset(cart->prg_rom + (size_t)i * 0x8000, i + 1, 0x8000);

    size_t chr_len = (size_t)num_chr * 0x2000;
    cart->chr_rom = calloc(1, chr_len);
    cart->chr_rom_len = chr_len;
    cart->chr_ram_allocated = 0;
    for (uint8_t i = 0; i < num_chr; i++)
        memset(cart->chr_rom + (size_t)i * 0x2000, 0x10 + i, 0x2000);

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge    = cart;
    map.mapper_id    = 66;
    map.num_prg_rom  = num_prg * 2;
    map.num_chr_rom  = num_chr;
    map.cpu_read     = mapper_066_cpu_read;
    map.cpu_write    = mapper_066_cpu_write;
    map.ppu_read     = mapper_066_ppu_read;
    map.ppu_write    = mapper_066_ppu_write;
    mapper_066_init(&map);
    return &map;
}

/* test_power_up_state
 *
 * At power-up register = 0: PRG bank 0 at $8000, CHR bank 0 at $0000. */
static void test_power_up_state(void) {
    printf("\n[test_power_up_state]\n");
    struct mapper *map = make_mapper(4, 4);

    uint8_t prg = mapper_066_cpu_read(map, 0x8000);
    ASSERT(prg == 1, "power-up: $8000 reads PRG bank 0 (filled with 1)");

    uint8_t chr = mapper_066_ppu_read(map, 0x0000);
    ASSERT(chr == 0x10, "power-up: $0000 reads CHR bank 0 (filled with 0x10)");
}

/* test_prg_bank_switch
 *
 * Bits 4-5 select the 32KB PRG bank. */
static void test_prg_bank_switch(void) {
    printf("\n[test_prg_bank_switch]\n");
    struct mapper *map = make_mapper(4, 4);

    mapper_066_cpu_write(map, 0x8000, 0x20); /* PRG bank 2 (bits 4-5 = 2) */
    uint8_t v = mapper_066_cpu_read(map, 0x8000);
    ASSERT(v == 3, "PRG bank 2 selected: $8000 reads 3");

    mapper_066_cpu_write(map, 0x8000, 0x30); /* PRG bank 3 */
    v = mapper_066_cpu_read(map, 0xFFFF);
    ASSERT(v == 4, "PRG bank 3 selected: $FFFF reads 4");
}

/* test_chr_bank_switch
 *
 * Bits 0-1 select the 8KB CHR bank. */
static void test_chr_bank_switch(void) {
    printf("\n[test_chr_bank_switch]\n");
    struct mapper *map = make_mapper(4, 4);

    mapper_066_cpu_write(map, 0x8000, 0x02); /* CHR bank 2 (bits 0-1 = 2) */
    uint8_t v = mapper_066_ppu_read(map, 0x0000);
    ASSERT(v == 0x12, "CHR bank 2 selected: $0000 reads 0x12");

    mapper_066_cpu_write(map, 0x8000, 0x03); /* CHR bank 3 */
    v = mapper_066_ppu_read(map, 0x1FFF);
    ASSERT(v == 0x13, "CHR bank 3 selected: $1FFF reads 0x13");
}

/* test_prg_and_chr_independent
 *
 * PRG uses bits 4-5, CHR uses bits 0-1 — same byte, independently decoded. */
static void test_prg_and_chr_independent(void) {
    printf("\n[test_prg_and_chr_independent]\n");
    struct mapper *map = make_mapper(4, 4);

    /* 0x13: PRG bank = bits 4-5 = 1, CHR bank = bits 0-1 = 3 */
    mapper_066_cpu_write(map, 0x8000, 0x13);

    uint8_t prg = mapper_066_cpu_read(map, 0x8000);
    ASSERT(prg == 2, "0x13 write: PRG bank 1 (filled with 2)");

    uint8_t chr = mapper_066_ppu_read(map, 0x0000);
    ASSERT(chr == 0x13, "0x13 write: CHR bank 3 (filled with 0x13)");
}

/* test_chr_rom_write_nop
 *
 * GxROM uses CHR-ROM — PPU writes must be silently ignored. */
static void test_chr_rom_write_nop(void) {
    printf("\n[test_chr_rom_write_nop]\n");
    struct mapper *map = make_mapper(4, 4);

    uint8_t before = mapper_066_ppu_read(map, 0x0010);
    mapper_066_ppu_write(map, 0x0010, 0xFF);
    uint8_t after = mapper_066_ppu_read(map, 0x0010);
    ASSERT(before == after, "CHR-ROM write is a no-op; value unchanged");
}

/* test_prg_full_32kb_window
 *
 * Both $8000-$BFFF and $C000-$FFFF come from the same 32KB bank. */
static void test_prg_full_32kb_window(void) {
    printf("\n[test_prg_full_32kb_window]\n");
    struct mapper *map = make_mapper(4, 4);

    mapper_066_cpu_write(map, 0x8000, 0x10); /* PRG bank 1 */
    uint8_t lo = mapper_066_cpu_read(map, 0x8000);
    uint8_t hi = mapper_066_cpu_read(map, 0xFFFF);
    ASSERT(lo == 2 && hi == 2, "bank 1 spans full $8000-$FFFF window");
}

int main(void) {
    printf("=== Mapper 066 (GxROM) unit tests ===\n");

    test_power_up_state();
    test_prg_bank_switch();
    test_chr_bank_switch();
    test_prg_and_chr_independent();
    test_chr_rom_write_nop();
    test_prg_full_32kb_window();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
