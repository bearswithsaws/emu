/*
 * test_mapper_003.c
 *
 * Unit tests for the CNROM (Mapper 003) implementation.
 *
 * CNROM: PRG-ROM is fixed (16KB mirrored or 32KB straight — no banking).
 * CHR-ROM is bank-switched in 8KB pages via writes to $8000-$FFFF.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_003.h"
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

/* Build a cartridge with num_prg 16KB PRG banks and num_chr 8KB CHR-ROM banks.
 * PRG bank 0 filled with 0xAA, bank 1 (if present) with 0xBB.
 * CHR bank i filled with byte (0x10 + i). */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = num_chr;
    cart->hdr = hdr;
    cart->mapper_id = 3;

    size_t prg_len = (size_t)num_prg * 0x4000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    cart->prg_rom[0] = 0xAA;
    if (num_prg > 1)
        memset(cart->prg_rom + 0x4000, 0xBB, 0x4000);

    size_t chr_len = (size_t)num_chr * 0x2000;
    cart->chr_rom = calloc(1, chr_len);
    cart->chr_rom_len = chr_len;
    cart->chr_ram_allocated = 0;
    for (uint8_t i = 0; i < num_chr; i++)
        memset(cart->chr_rom + i * 0x2000, 0x10 + i, 0x2000);

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge    = cart;
    map.mapper_id    = 3;
    map.num_prg_rom  = num_prg;
    map.num_chr_rom  = num_chr;
    map.cpu_read     = mapper_003_cpu_read;
    map.cpu_write    = mapper_003_cpu_write;
    map.ppu_read     = mapper_003_ppu_read;
    map.ppu_write    = mapper_003_ppu_write;
    mapper_003_init(&map);
    return &map;
}

/* test_prg_16kb_mirror
 *
 * With 1 PRG bank (16KB), $8000-$BFFF and $C000-$FFFF should both map to
 * the same bank (addr & 0x3FFF). */
static void test_prg_16kb_mirror(void) {
    printf("\n[test_prg_16kb_mirror]\n");

    struct mapper *map = make_mapper(1, 1);

    uint8_t lo = mapper_003_cpu_read(map, 0x8000);
    uint8_t hi = mapper_003_cpu_read(map, 0xC000);
    ASSERT(lo == 0xAA, "16KB PRG: $8000 reads bank 0");
    ASSERT(hi == 0xAA, "16KB PRG: $C000 mirrors to bank 0");
}

/* test_prg_32kb_no_mirror
 *
 * With 2 PRG banks (32KB), $8000-$BFFF maps to bank 0 and $C000-$FFFF to
 * bank 1. No mirroring needed. */
static void test_prg_32kb_no_mirror(void) {
    printf("\n[test_prg_32kb_no_mirror]\n");

    struct mapper *map = make_mapper(2, 1);

    uint8_t lo = mapper_003_cpu_read(map, 0x8000);
    uint8_t hi = mapper_003_cpu_read(map, 0xC000);
    ASSERT(lo == 0xAA, "32KB PRG: $8000 reads bank 0");
    ASSERT(hi == 0xBB, "32KB PRG: $C000 reads bank 1");
}

/* test_chr_bank_select
 *
 * With 4 CHR banks, write bank index to $8000; PPU $0000 should read from
 * the selected bank. */
static void test_chr_bank_select(void) {
    printf("\n[test_chr_bank_select]\n");

    struct mapper *map = make_mapper(1, 4);

    /* Default: bank 0 */
    uint8_t v0 = mapper_003_ppu_read(map, 0x0000);
    ASSERT(v0 == 0x10, "CHR default: bank 0 reads 0x10");

    mapper_003_cpu_write(map, 0x8000, 1);
    uint8_t v1 = mapper_003_ppu_read(map, 0x0000);
    ASSERT(v1 == 0x11, "CHR select 1: reads 0x11");

    mapper_003_cpu_write(map, 0x8000, 3);
    uint8_t v3 = mapper_003_ppu_read(map, 0x0000);
    ASSERT(v3 == 0x13, "CHR select 3: reads 0x13");
}

/* test_chr_bank_low_2_bits
 *
 * CNROM uses only 2 bits for the CHR bank (0-3). Writing 0x05 should
 * select bank 1 (0x05 & 0x03 = 1). */
static void test_chr_bank_low_2_bits(void) {
    printf("\n[test_chr_bank_low_2_bits]\n");

    struct mapper *map = make_mapper(1, 4);

    mapper_003_cpu_write(map, 0x8000, 0x05);
    uint8_t v = mapper_003_ppu_read(map, 0x0000);
    ASSERT(v == 0x11, "CHR bank masked to 2 bits: 0x05 → bank 1");
}

/* test_chr_write_to_rom_is_nop
 *
 * CNROM has CHR-ROM, not CHR-RAM.  PPU writes should be ignored. */
static void test_chr_write_to_rom_is_nop(void) {
    printf("\n[test_chr_write_to_rom_is_nop]\n");

    struct mapper *map = make_mapper(1, 2);

    uint8_t before = mapper_003_ppu_read(map, 0x0010);
    mapper_003_ppu_write(map, 0x0010, 0xFF);
    uint8_t after = mapper_003_ppu_read(map, 0x0010);
    ASSERT(before == after, "CHR-ROM write is a no-op");
}

/* test_chr_bank_offset
 *
 * Reading from various offsets within a bank should produce the same fill
 * byte. */
static void test_chr_bank_offset(void) {
    printf("\n[test_chr_bank_offset]\n");

    struct mapper *map = make_mapper(1, 3);

    mapper_003_cpu_write(map, 0x8000, 2);

    uint8_t v0 = mapper_003_ppu_read(map, 0x0000);
    uint8_t v7 = mapper_003_ppu_read(map, 0x1FFF);
    ASSERT(v0 == 0x12, "CHR bank 2: base offset reads 0x12");
    ASSERT(v7 == 0x12, "CHR bank 2: last offset reads 0x12");
}

int main(void) {
    printf("=== Mapper 003 (CNROM) unit tests ===\n");

    test_prg_16kb_mirror();
    test_prg_32kb_no_mirror();
    test_chr_bank_select();
    test_chr_bank_low_2_bits();
    test_chr_write_to_rom_is_nop();
    test_chr_bank_offset();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
