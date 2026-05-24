/*
 * test_mapper_009.c
 *
 * Unit tests for the PxROM (Mapper 009 / MMC2) implementation.
 *
 * PRG: 128 KB, four 8 KB windows — switchable at $8000, fixed last-3/last-2/last at $A000.
 * CHR: 128 KB, two 4 KB windows each driven by a latch (FD/FE).
 * Latch triggers: reads at $0FD0-$0FDF → latch0=FD, $0FE0-$0FEF → latch0=FE,
 *                             $1FD0-$1FDF → latch1=FD, $1FE0-$1FEF → latch1=FE.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_009.h"
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
 * make_cart — synthetic 128 KB PRG + 128 KB CHR cartridge.
 * PRG 8 KB bank i is filled with byte (i + 1).
 * CHR 4 KB bank i is filled with byte (i + 0x10).
 */
static struct nes_cartridge *make_cart(void) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    /* 128 KB PRG = 8 × 16 KB iNES units */
    hdr->prg_rom_size = 8;
    /* 128 KB CHR = 16 × 8 KB iNES units */
    hdr->chr_rom_size = 16;
    cart->hdr = hdr;
    cart->mapper_id = 9;

    /* PRG: 16 × 8 KB banks filled with (bank_index + 1) */
    size_t prg_len = 16 * 0x2000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = (uint32_t)prg_len;
    for (int i = 0; i < 16; i++)
        memset(cart->prg_rom + (size_t)i * 0x2000, i + 1, 0x2000);

    /* CHR: 32 × 4 KB banks filled with (bank_index + 0x10) */
    size_t chr_len = 32 * 0x1000;
    cart->chr_rom = calloc(1, chr_len);
    cart->chr_rom_len = (uint32_t)chr_len;
    for (int i = 0; i < 32; i++)
        memset(cart->chr_rom + (size_t)i * 0x1000, i + 0x10, 0x1000);

    cart->chr_ram_allocated = 0;
    return cart;
}

static struct mapper *make_mapper(void) {
    struct nes_cartridge *cart = make_cart();
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge   = cart;
    map.mapper_id   = 9;
    map.num_prg_rom = 8;   /* 8 × 16 KB = 128 KB */
    map.num_chr_rom = 16;  /* 16 × 8 KB = 128 KB */
    map.cpu_read    = mapper_009_cpu_read;
    map.cpu_write   = mapper_009_cpu_write;
    map.ppu_read    = mapper_009_ppu_read;
    map.ppu_write   = mapper_009_ppu_write;
    mapper_009_init(&map);
    return &map;
}

/* test_power_up_state
 *
 * At power-up:
 *   - PRG switchable bank = 0 → $8000 reads bank 0 (filled with 1)
 *   - Fixed banks: $A000=bank13, $C000=bank14, $E000=bank15
 *   - Both latches = FE
 */
static void test_power_up_state(void) {
    printf("\n[test_power_up_state]\n");
    struct mapper *map = make_mapper();

    ASSERT(mapper_009_cpu_read(map, 0x8000) == 1,
           "power-up: $8000 → PRG bank 0 (filled with 1)");
    ASSERT(mapper_009_cpu_read(map, 0x9FFF) == 1,
           "power-up: $9FFF → PRG bank 0 (filled with 1)");
    ASSERT(mapper_009_cpu_read(map, 0xA000) == 14,
           "power-up: $A000 → PRG bank 13 (filled with 14)");
    ASSERT(mapper_009_cpu_read(map, 0xC000) == 15,
           "power-up: $C000 → PRG bank 14 (filled with 15)");
    ASSERT(mapper_009_cpu_read(map, 0xE000) == 16,
           "power-up: $E000 → PRG bank 15 (filled with 16)");
    ASSERT(mapper_009_cpu_read(map, 0xFFFF) == 16,
           "power-up: $FFFF → PRG bank 15 (filled with 16)");
}

/* test_prg_bank_switch
 *
 * Write bank 5 to $A000 → $8000-$9FFF reads from bank 5 (filled with 6).
 * Fixed windows are unaffected.
 */
static void test_prg_bank_switch(void) {
    printf("\n[test_prg_bank_switch]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xA000, 5);
    ASSERT(mapper_009_cpu_read(map, 0x8000) == 6,
           "prg switch: bank 5 at $8000 (filled with 6)");
    ASSERT(mapper_009_cpu_read(map, 0x9FFF) == 6,
           "prg switch: bank 5 at $9FFF (filled with 6)");
    /* fixed banks unchanged */
    ASSERT(mapper_009_cpu_read(map, 0xE000) == 16,
           "prg switch: fixed last bank still 15 (filled with 16)");
}

/* test_prg_bank_mask
 *
 * PRG bank register is 4 bits (0x0F mask).
 */
static void test_prg_bank_mask(void) {
    printf("\n[test_prg_bank_mask]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xA000, 0xFF); /* only bits 3:0 kept → bank 15 */
    ASSERT(mapper_009_cpu_read(map, 0x8000) == 16,
           "prg mask: 0xFF & 0x0F = 15, bank 15 (filled with 16)");
}

/* test_chr_latch0_power_up_fe
 *
 * Both latches start at FE.  Writing chr_bank[1] (lo/FE slot) to bank 7
 * and chr_bank[0] (lo/FD slot) to bank 3, then reading $0000 should return
 * the FE slot's data (bank 7, filled with 0x17).
 */
static void test_chr_latch0_power_up_fe(void) {
    printf("\n[test_chr_latch0_power_up_fe]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xB000, 3); /* lo/FD → bank 3 */
    mapper_009_cpu_write(map, 0xC000, 7); /* lo/FE → bank 7 */

    /* latch0 = FE at power-up → reads bank 7 (0x17) */
    ASSERT(mapper_009_ppu_read(map, 0x0000) == 0x17,
           "chr latch0=FE at power-up: $0000 reads bank 7 (0x17)");
    ASSERT(mapper_009_ppu_read(map, 0x0FFF) == 0x17,
           "chr latch0=FE at power-up: $0FFF reads bank 7 (0x17)");
}

/* test_chr_latch0_fd_switch
 *
 * After a read from $0FD0 the latch switches to FD.
 * The trigger read itself returns FE-bank data (latch fires after).
 * Subsequent reads use the FD bank.
 */
static void test_chr_latch0_fd_switch(void) {
    printf("\n[test_chr_latch0_fd_switch]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xB000, 3); /* lo/FD → bank 3 (0x13) */
    mapper_009_cpu_write(map, 0xC000, 7); /* lo/FE → bank 7 (0x17) */

    /* trigger read at $0FD0 returns FE-bank data, then latch flips to FD */
    uint8_t trigger_val = mapper_009_ppu_read(map, 0x0FD0);
    ASSERT(trigger_val == 0x17,
           "latch0 FD trigger: read itself returns pre-flip FE bank (0x17)");

    /* next read from $0000 now uses FD bank */
    ASSERT(mapper_009_ppu_read(map, 0x0000) == 0x13,
           "latch0 FD trigger: subsequent $0000 reads FD bank (0x13)");
}

/* test_chr_latch0_fe_retrigger
 *
 * After latch0 is FD, reading $0FE0 switches it back to FE.
 */
static void test_chr_latch0_fe_retrigger(void) {
    printf("\n[test_chr_latch0_fe_retrigger]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xB000, 3); /* lo/FD → bank 3 */
    mapper_009_cpu_write(map, 0xC000, 7); /* lo/FE → bank 7 */

    mapper_009_ppu_read(map, 0x0FD0); /* flip to FD */
    ASSERT(mapper_009_ppu_read(map, 0x0000) == 0x13, "after FD trigger: reads FD bank");

    mapper_009_ppu_read(map, 0x0FE0); /* flip back to FE */
    ASSERT(mapper_009_ppu_read(map, 0x0000) == 0x17, "after FE retrigger: reads FE bank");
}

/* test_chr_latch1
 *
 * Same latch/trigger logic for the hi window ($1000-$1FFF).
 */
static void test_chr_latch1(void) {
    printf("\n[test_chr_latch1]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xD000, 4); /* hi/FD → bank 4 (0x14) */
    mapper_009_cpu_write(map, 0xE000, 9); /* hi/FE → bank 9 (0x19) */

    /* power-up: latch1=FE → reads bank 9 */
    ASSERT(mapper_009_ppu_read(map, 0x1000) == 0x19,
           "latch1=FE at power-up: $1000 reads bank 9 (0x19)");

    /* trigger FD via $1FD0 */
    mapper_009_ppu_read(map, 0x1FD0);
    ASSERT(mapper_009_ppu_read(map, 0x1000) == 0x14,
           "latch1 FD trigger: $1000 reads bank 4 (0x14)");

    /* trigger FE via $1FE0 */
    mapper_009_ppu_read(map, 0x1FE0);
    ASSERT(mapper_009_ppu_read(map, 0x1000) == 0x19,
           "latch1 FE retrigger: $1000 reads bank 9 (0x19)");
}

/* test_latches_independent
 *
 * latch0 and latch1 are independent — triggering one does not affect the other.
 */
static void test_latches_independent(void) {
    printf("\n[test_latches_independent]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xB000, 2); /* lo/FD → bank 2 (0x12) */
    mapper_009_cpu_write(map, 0xC000, 6); /* lo/FE → bank 6 (0x16) */
    mapper_009_cpu_write(map, 0xD000, 4); /* hi/FD → bank 4 (0x14) */
    mapper_009_cpu_write(map, 0xE000, 9); /* hi/FE → bank 9 (0x19) */

    mapper_009_ppu_read(map, 0x0FD0); /* flip latch0 to FD */

    ASSERT(mapper_009_ppu_read(map, 0x0000) == 0x12,
           "after latch0→FD: lo window reads FD bank");
    /* latch1 should remain FE */
    ASSERT(mapper_009_ppu_read(map, 0x1000) == 0x19,
           "after latch0→FD: hi window still reads FE bank (latches independent)");
}

/* test_chr_bank_register_mask
 *
 * CHR bank registers are 5 bits (0x1F mask).
 */
static void test_chr_bank_register_mask(void) {
    printf("\n[test_chr_bank_register_mask]\n");
    struct mapper *map = make_mapper();

    /* Write 0xFF → masked to 0x1F = 31; 32 banks total so bank 31 = 0x10+31=0x2F */
    mapper_009_cpu_write(map, 0xC000, 0xFF); /* lo/FE → bank 31 */
    ASSERT(mapper_009_ppu_read(map, 0x0000) == 0x2F,
           "chr mask: 0xFF & 0x1F = 31, bank 31 (0x2F)");
}

/* test_mirroring
 *
 * $F000 bit 0: 0=vertical, 1=horizontal.
 */
static void test_mirroring(void) {
    printf("\n[test_mirroring]\n");
    struct mapper *map = make_mapper();

    mapper_009_cpu_write(map, 0xF000, 0x01);
    ASSERT(map->mirroring == MIRROR_HORIZONTAL, "mirroring: bit 0=1 → horizontal");

    mapper_009_cpu_write(map, 0xF000, 0x00);
    ASSERT(map->mirroring == MIRROR_VERTICAL, "mirroring: bit 0=0 → vertical");
}

int main(void) {
    printf("=== Mapper 009 (PxROM / MMC2) unit tests ===\n");

    test_power_up_state();
    test_prg_bank_switch();
    test_prg_bank_mask();
    test_chr_latch0_power_up_fe();
    test_chr_latch0_fd_switch();
    test_chr_latch0_fe_retrigger();
    test_chr_latch1();
    test_latches_independent();
    test_chr_bank_register_mask();
    test_mirroring();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
