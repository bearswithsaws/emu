/*
 * test_mapper_004.c
 *
 * Unit tests for the MMC3 (Mapper 004) implementation.
 *
 * MMC3: up to 512KB PRG-ROM in four 8KB windows ($8000/$A000/$C000/$E000).
 * R6/R7 are switchable; R6 or $C000 is fixed depending on PRG mode bit.
 * CHR-ROM: up to 2MB banked in eight 1KB windows.
 * R0/R1 select 2KB aligned pairs; R2-R5 select individual 1KB banks.
 * Scanline IRQ counter decrements each scanline callback.
 *
 * PRG-RAM: 8KB at $6000-$7FFF.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_004.h"
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

/* Build a cartridge with num_prg 16KB PRG banks (num_prg*2 = 8KB banks) and
 * num_chr 8KB CHR banks (num_chr*8 = 1KB banks).
 * PRG 8KB bank i is filled with byte (i + 1).
 * CHR 1KB bank j is filled with byte (0x20 + j). */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = num_chr;
    cart->hdr = hdr;
    cart->mapper_id = 4;

    size_t prg_len = (size_t)num_prg * 0x4000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    uint16_t n8kb = num_prg * 2;
    for (uint16_t i = 0; i < n8kb; i++)
        memset(cart->prg_rom + i * 0x2000, (uint8_t)(i + 1), 0x2000);

    size_t chr_len = (size_t)num_chr * 0x2000;
    cart->chr_rom = calloc(1, chr_len);
    cart->chr_rom_len = chr_len;
    cart->chr_ram_allocated = 0;
    uint16_t n1kb = num_chr * 8;
    for (uint16_t j = 0; j < n1kb; j++)
        memset(cart->chr_rom + j * 0x0400, (uint8_t)(0x20 + j), 0x0400);

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge    = cart;
    map.mapper_id    = 4;
    map.num_prg_rom  = num_prg;
    map.num_chr_rom  = num_chr;
    map.cpu_read     = mapper_004_cpu_read;
    map.cpu_write    = mapper_004_cpu_write;
    map.ppu_read     = mapper_004_ppu_read;
    map.ppu_write    = mapper_004_ppu_write;
    map.scanline     = mapper_004_scanline;
    mapper_004_init(&map);
    return &map;
}

/* Write to bank select register then the data register.
 * bank_select_val: the value for the $8000 even write.
 * data_val: the value for the $8001 odd write. */
static void set_bank(struct mapper *map, uint8_t bank_select_val, uint8_t data_val) {
    mapper_004_cpu_write(map, 0x8000, bank_select_val); /* even */
    mapper_004_cpu_write(map, 0x8001, data_val);        /* odd  */
}

/* test_power_up_prg
 *
 * At power-up all registers are 0.  PRG mode 0: R6 maps $8000, $C000 is
 * fixed to second-to-last 8KB bank, $E000 is fixed to last bank.
 * With 4 16KB banks (8 8KB banks, n8kb=8):
 *   $8000 → R6=0 → bank 0 (filled 0x01)
 *   $A000 → R7=0 → bank 0 (filled 0x01)
 *   $C000 → fixed = n8kb-2 = bank 6 (filled 0x07)
 *   $E000 → fixed = n8kb-1 = bank 7 (filled 0x08)
 */
static void test_power_up_prg(void) {
    printf("\n[test_power_up_prg]\n");

    struct mapper *map = make_mapper(4, 1);

    uint8_t v8 = mapper_004_cpu_read(map, 0x8000);
    ASSERT(v8 == 1, "power-up: $8000 = bank 0 (R6=0, filled 0x01)");

    uint8_t vc = mapper_004_cpu_read(map, 0xC000);
    ASSERT(vc == 7, "power-up: $C000 = second-to-last bank 6 (filled 0x07)");

    uint8_t ve = mapper_004_cpu_read(map, 0xE000);
    ASSERT(ve == 8, "power-up: $E000 = last bank 7 (filled 0x08)");
}

/* test_prg_r6_switch
 *
 * Set bank_select to 6 (select R6), write bank 3.  $8000 should map to
 * 8KB bank 3 (filled 0x04). */
static void test_prg_r6_switch(void) {
    printf("\n[test_prg_r6_switch]\n");

    struct mapper *map = make_mapper(4, 1);

    set_bank(map, 6, 3); /* R6 = 3 */

    uint8_t v = mapper_004_cpu_read(map, 0x8000);
    ASSERT(v == 4, "R6=3: $8000 reads 8KB bank 3 (filled 0x04)");
}

/* test_prg_mode1_swap
 *
 * PRG mode bit (bit 6 of bank_select write) swaps $8000 and $C000 roles:
 * in mode 1, $8000 is fixed to second-to-last, $C000 switches via R6.
 * Write bank_select 0xC6 (bit7=1 means chr_invert, bit6=1 means prg_mode).
 * Actually: bit7 = chr_invert, bit6 = prg_mode, bits[2:0] = register select.
 * We want prg_mode=1, register=6: value = 0b01000110 = 0x46. */
static void test_prg_mode1_swap(void) {
    printf("\n[test_prg_mode1_swap]\n");

    struct mapper *map = make_mapper(4, 1); /* 8 8KB banks */

    /* Set prg_mode=1 and select R6, then set R6=2. */
    mapper_004_cpu_write(map, 0x8000, 0x46); /* prg_mode=1, select R6 */
    mapper_004_cpu_write(map, 0x8001, 2);    /* R6 = 2 */

    /* In PRG mode 1: $C000 = R6 = bank 2 (filled 0x03). */
    uint8_t vc = mapper_004_cpu_read(map, 0xC000);
    ASSERT(vc == 3, "PRG mode 1: $C000 = R6=2 (filled 0x03)");

    /* $8000 = fixed to second-to-last = bank 6 (filled 0x07). */
    uint8_t v8 = mapper_004_cpu_read(map, 0x8000);
    ASSERT(v8 == 7, "PRG mode 1: $8000 fixed to second-to-last bank (filled 0x07)");
}

/* test_prg_ram
 *
 * PRG-RAM at $6000-$7FFF: write then read back. */
static void test_prg_ram(void) {
    printf("\n[test_prg_ram]\n");

    struct mapper *map = make_mapper(2, 1);

    mapper_004_cpu_write(map, 0x6100, 0x55);
    uint8_t v = mapper_004_cpu_read(map, 0x6100);
    ASSERT(v == 0x55, "PRG-RAM: write then read-back matches");
}

/* test_chr_r0_2kb
 *
 * R0 selects a 2KB aligned CHR pair for $0000-$07FF (CHR normal mode,
 * chr_invert=0).  R0=4 → 1KB banks 4 and 5 → $0000=$0020+4=0x24,
 * $0400=$0020+5=0x25. */
static void test_chr_r0_2kb(void) {
    printf("\n[test_chr_r0_2kb]\n");

    /* 2 8KB CHR banks = 16 1KB banks. */
    struct mapper *map = make_mapper(2, 2);

    set_bank(map, 0, 4); /* R0 = 4 (2KB pair: 1KB banks 4 and 5) */

    uint8_t v0 = mapper_004_ppu_read(map, 0x0000);
    ASSERT(v0 == 0x24, "CHR R0=4: $0000 reads 1KB bank 4 (filled 0x24)");

    uint8_t v4 = mapper_004_ppu_read(map, 0x0400);
    ASSERT(v4 == 0x25, "CHR R0=4: $0400 reads 1KB bank 5 (filled 0x25)");
}

/* test_chr_r2_1kb
 *
 * R2 selects a 1KB CHR bank for $1000 (CHR normal mode, chr_invert=0).
 * R2=7 → $1000 reads 1KB bank 7 (filled 0x27). */
static void test_chr_r2_1kb(void) {
    printf("\n[test_chr_r2_1kb]\n");

    struct mapper *map = make_mapper(2, 2);

    set_bank(map, 2, 7); /* R2 = 7 */

    uint8_t v = mapper_004_ppu_read(map, 0x1000);
    ASSERT(v == 0x27, "CHR R2=7: $1000 reads 1KB bank 7 (filled 0x27)");
}

/* test_chr_invert
 *
 * chr_invert=1 swaps which half of CHR space gets the 2KB banks.
 * With chr_invert set (bit7 of bank_select write): R0 drives $1000-$17FF
 * and R2 drives $0000-$03FF.
 * bank_select = 0x80 | 0 = 0x80 to set chr_invert and select R0. */
static void test_chr_invert(void) {
    printf("\n[test_chr_invert]\n");

    struct mapper *map = make_mapper(2, 2);

    /* Set chr_invert=1, select R0, set R0=4 */
    mapper_004_cpu_write(map, 0x8000, 0x80); /* chr_invert=1, register=0 */
    mapper_004_cpu_write(map, 0x8001, 4);    /* R0=4 */

    /* With chr_invert, R0 drives $1000-$17FF (1KB bank 4 = 0x24). */
    uint8_t v1 = mapper_004_ppu_read(map, 0x1000);
    ASSERT(v1 == 0x24, "chr_invert: R0=4 at $1000 reads 0x24");

    /* Set R2=2, which now drives $0000-$03FF. */
    mapper_004_cpu_write(map, 0x8000, 0x82); /* chr_invert=1, register=2 */
    mapper_004_cpu_write(map, 0x8001, 2);    /* R2=2 */

    uint8_t v0 = mapper_004_ppu_read(map, 0x0000);
    ASSERT(v0 == 0x22, "chr_invert: R2=2 at $0000 reads 0x22");
}

/* test_mirroring
 *
 * Write to $A000 (even) sets mirroring: 0=vertical, 1=horizontal. */
static void test_mirroring(void) {
    printf("\n[test_mirroring]\n");

    struct mapper *map = make_mapper(2, 1);

    mapper_004_cpu_write(map, 0xA000, 0); /* vertical */
    ASSERT(map->mirroring == MIRROR_VERTICAL, "mirroring: $A000=0 → vertical");

    mapper_004_cpu_write(map, 0xA000, 1); /* horizontal */
    ASSERT(map->mirroring == MIRROR_HORIZONTAL, "mirroring: $A000=1 → horizontal");
}

/* test_irq_counter
 *
 * Load latch=2, trigger reload, fire 3 scanlines. The first scanline reloads
 * the counter from the latch (counter=2); the next two decrement it to 0,
 * firing the IRQ on the third scanline. */
static void test_irq_counter(void) {
    printf("\n[test_irq_counter]\n");

    struct mapper *map = make_mapper(2, 1);

    mapper_004_cpu_write(map, 0xC000, 2); /* latch=2 */
    mapper_004_cpu_write(map, 0xC001, 0); /* force reload */
    mapper_004_cpu_write(map, 0xE001, 0); /* IRQ enable */

    mapper_004_scanline(map); /* reload: counter=2 */
    ASSERT(map->irq_pending == 0, "IRQ: not pending after reload scanline (counter=2)");

    mapper_004_scanline(map); /* decrement: counter=1 */
    ASSERT(map->irq_pending == 0, "IRQ: not pending after 2nd scanline (counter=1)");

    mapper_004_scanline(map); /* decrement: counter=0, fires */
    ASSERT(map->irq_pending == 1, "IRQ: pending after 3rd scanline (counter=0)");
}

/* test_irq_disable_clears_pending
 *
 * Writing to $E000 (IRQ disable) clears the pending flag. */
static void test_irq_disable_clears_pending(void) {
    printf("\n[test_irq_disable_clears_pending]\n");

    struct mapper *map = make_mapper(2, 1);

    mapper_004_cpu_write(map, 0xC000, 1);
    mapper_004_cpu_write(map, 0xC001, 0);
    mapper_004_cpu_write(map, 0xE001, 0); /* enable */

    mapper_004_scanline(map); /* counter=1, reload */
    mapper_004_scanline(map); /* counter=0, fires */
    ASSERT(map->irq_pending == 1, "IRQ fires");

    mapper_004_cpu_write(map, 0xE000, 0); /* disable + clear */
    ASSERT(map->irq_pending == 0, "IRQ cleared by $E000 write");
}

int main(void) {
    printf("=== Mapper 004 (MMC3) unit tests ===\n");

    test_power_up_prg();
    test_prg_r6_switch();
    test_prg_mode1_swap();
    test_prg_ram();
    test_chr_r0_2kb();
    test_chr_r2_1kb();
    test_chr_invert();
    test_mirroring();
    test_irq_counter();
    test_irq_disable_clears_pending();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
