/*
 * test_mapper_005.c
 *
 * Unit tests for the MMC5 (ExROM, Mapper 005) implementation.
 *
 * Coverage:
 *   PRG mode 0 (32 KB window)
 *   PRG mode 1 (2 × 16 KB windows)
 *   PRG mode 2 (16 KB + 8 KB + 8 KB)
 *   PRG mode 3 (4 × 8 KB windows — default)
 *   PRG-RAM at $6000-$7FFF via $5113
 *   PRG-RAM write-protect registers
 *   CHR mode 0 (8 KB)
 *   CHR mode 1 (2 × 4 KB)
 *   CHR mode 2 (4 × 2 KB)
 *   CHR mode 3 (8 × 1 KB — default)
 *   ExRAM R/W ($5C00-$5FFF, mode 2)
 *   Nametable fill-mode (CIRAM=NULL, fill tile/attr)
 *   Scanline IRQ fires at correct count
 *   Scanline IRQ enable/disable
 *   Multiply unit ($5205 / $5206)
 *
 * Compatible games: Castlevania III, Just Breed, Metal Slader Glory.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "mapper.h"
#include "mapper_005.h"

static int test_pass = 0;
static int test_fail = 0;

#define ASSERT(cond, msg)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  PASS: %s\n", msg);                                       \
            test_pass++;                                                       \
        } else {                                                               \
            printf("  FAIL: %s  (line %d)\n", msg, __LINE__);                  \
            test_fail++;                                                       \
        }                                                                      \
    } while (0)

/* -------------------------------------------------------------------------
 * Cartridge / mapper factory helpers
 * -------------------------------------------------------------------------
 * num_prg : number of 16 KB PRG-ROM banks   (num_prg * 2 = number of 8 KB
 * banks) num_chr : number of 8 KB CHR-ROM banks    (num_chr * 8 = number of 1
 * KB banks)
 *
 * PRG 8 KB bank i is filled with (i + 1).
 * CHR 1 KB bank j is filled with (0x20 + j).
 */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = num_chr;
    cart->hdr = hdr;
    cart->mapper_id = 5;

    size_t prg_len = (size_t)num_prg * 0x4000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    uint16_t n8kb = (uint16_t)(num_prg * 2);
    for (uint16_t i = 0; i < n8kb; i++)
        memset(cart->prg_rom + i * 0x2000, (uint8_t)(i + 1), 0x2000);

    if (num_chr > 0) {
        size_t chr_len = (size_t)num_chr * 0x2000;
        cart->chr_rom = calloc(1, chr_len);
        cart->chr_rom_len = chr_len;
        cart->chr_ram_allocated = 0;
        uint16_t n1kb = (uint16_t)(num_chr * 8);
        for (uint16_t j = 0; j < n1kb; j++)
            memset(cart->chr_rom + j * 0x0400, (uint8_t)(0x20 + j), 0x0400);
    }

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr);
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge = cart;
    map.mapper_id = 5;
    map.num_prg_rom = num_prg;
    map.num_chr_rom = num_chr;
    map.cpu_read = mapper_005_cpu_read;
    map.cpu_write = mapper_005_cpu_write;
    map.ppu_read = mapper_005_ppu_read;
    map.ppu_write = mapper_005_ppu_write;
    map.nt_read = mapper_005_nt_read;
    map.nt_write = mapper_005_nt_write;
    map.scanline = mapper_005_scanline;
    mapper_005_init(&map);
    return &map;
}

/* Unlock PRG-RAM writes (hardware requires $5102=0x02 AND $5103=0x01). */
static void unlock_ram(struct mapper *map) {
    mapper_005_cpu_write(map, 0x5102, 0x02);
    mapper_005_cpu_write(map, 0x5103, 0x01);
}

/* =========================================================================
 * test_prg_mode3_banks — default 4 × 8 KB (mode 3)
 * =========================================================================
 * With 4 × 16 KB PRG (8 × 8 KB banks), power-up $5117 = last bank (index 7).
 * After writing bank registers:
 *   $5114 = 0x81 → $8000 = ROM bank 1 (filled 0x02)
 *   $5115 = 0x83 → $A000 = ROM bank 3 (filled 0x04)
 *   $5116 = 0x85 → $C000 = ROM bank 5 (filled 0x06)
 *   $5117 = 0x87 → $E000 = ROM bank 7 (filled 0x08)
 */
static void test_prg_mode3_banks(void) {
    printf("\n[test_prg_mode3_banks]\n");
    struct mapper *map = make_mapper(4, 1);

    // Mode 3 is the power-up default; set it explicitly for clarity.
    mapper_005_cpu_write(map, 0x5100, 3);

    mapper_005_cpu_write(map, 0x5114, 0x81); // ROM bank 1
    mapper_005_cpu_write(map, 0x5115, 0x83); // ROM bank 3
    mapper_005_cpu_write(map, 0x5116, 0x85); // ROM bank 5
    mapper_005_cpu_write(map, 0x5117, 0x87); // ROM bank 7 (forced ROM)

    ASSERT(mapper_005_cpu_read(map, 0x8000) == 0x02,
           "mode3: $8000 → bank 1 (0x02)");
    ASSERT(mapper_005_cpu_read(map, 0xA000) == 0x04,
           "mode3: $A000 → bank 3 (0x04)");
    ASSERT(mapper_005_cpu_read(map, 0xC000) == 0x06,
           "mode3: $C000 → bank 5 (0x06)");
    ASSERT(mapper_005_cpu_read(map, 0xE000) == 0x08,
           "mode3: $E000 → bank 7 (0x08)");
}

/* =========================================================================
 * test_prg_mode0_32kb — single 32 KB window
 * =========================================================================
 * 4 × 16 KB PRG (8 × 8 KB).  In mode 0, $5117 bits 6-2 form the 32 KB bank.
 * Write $5117 = 0x88 (bit 7 = ROM forced, bits 3-2 = bank 2 of 32 KB = banks
 * 4-7). $8000 should read bank 4 (0x05), $E000 should read bank 7 (0x08).
 */
static void test_prg_mode0_32kb(void) {
    printf("\n[test_prg_mode0_32kb]\n");
    struct mapper *map = make_mapper(4, 1);

    mapper_005_cpu_write(map, 0x5100, 0); // mode 0
    // 32 KB bank 1 → 8 KB banks 4 (0x81 << 2 = 4, so raw = (1 << 2) | 0x80 =
    // 0x84) bits 6-2 select the 32KB bank; raw=0x84 means bits[6:2]=0b00100=4,
    // /4=1 → 32KB bank 1
    mapper_005_cpu_write(
        map, 0x5117, 0x84); // bits[6:2]=0b00100 → 32KB bank 1 = 8KB banks 4-7

    // $8000 = first 8KB of bank group → 8KB bank 4 (filled 0x05)
    ASSERT(mapper_005_cpu_read(map, 0x8000) == 0x05,
           "mode0: $8000 → first 8KB of 32KB bank 1 (0x05)");
    // $E000 = last 8KB of bank group → 8KB bank 7 (filled 0x08)
    ASSERT(mapper_005_cpu_read(map, 0xE000) == 0x08,
           "mode0: $E000 → last 8KB of 32KB bank 1 (0x08)");
}

/* =========================================================================
 * test_prg_mode1_16kb — two 16 KB windows
 * =========================================================================
 * $5115 = 0x82 (ROM 16KB bank 1 → 8KB banks 2-3, filled 0x03/0x04)
 * $5117 = 0x84 (ROM 16KB bank 2 → 8KB banks 4-5, filled 0x05/0x06)
 */
static void test_prg_mode1_16kb(void) {
    printf("\n[test_prg_mode1_16kb]\n");
    struct mapper *map = make_mapper(4, 1);

    mapper_005_cpu_write(map, 0x5100, 1);    // mode 1
    mapper_005_cpu_write(map, 0x5115, 0x82); // 16KB bank 1 (8KB banks 2-3)
    mapper_005_cpu_write(map, 0x5117, 0x84); // 16KB bank 2 (8KB banks 4-5)

    // $8000 = first byte of 16KB window → first byte of 8KB bank 2 (0x03)
    ASSERT(mapper_005_cpu_read(map, 0x8000) == 0x03,
           "mode1: $8000 → 16KB bank 1 lo (0x03)");
    // $C000 = first byte of second 16KB window → first byte of 8KB bank 4
    // (0x05)
    ASSERT(mapper_005_cpu_read(map, 0xC000) == 0x05,
           "mode1: $C000 → 16KB bank 2 lo (0x05)");
}

/* =========================================================================
 * test_prg_mode2_mixed — 16 KB + 8 KB + 8 KB
 * =========================================================================
 * $5115 = 0x82 (16KB bank 1 → 8KB 2-3); $5116 = 0x84 (8KB bank 4); $5117 =
 * last.
 */
static void test_prg_mode2_mixed(void) {
    printf("\n[test_prg_mode2_mixed]\n");
    struct mapper *map = make_mapper(4, 1);

    mapper_005_cpu_write(map, 0x5100, 2);    // mode 2
    mapper_005_cpu_write(map, 0x5115, 0x82); // 16KB bank 1 → 8KB banks 2-3
    mapper_005_cpu_write(map, 0x5116, 0x84); // 8KB bank 4
    mapper_005_cpu_write(map, 0x5117, 0x86); // 8KB bank 6 (forced ROM)

    ASSERT(mapper_005_cpu_read(map, 0x8000) == 0x03,
           "mode2: $8000 → 16KB-window lo (8KB bank 2 = 0x03)");
    ASSERT(mapper_005_cpu_read(map, 0xC000) == 0x05,
           "mode2: $C000 → 8KB bank 4 (0x05)");
    ASSERT(mapper_005_cpu_read(map, 0xE000) == 0x07,
           "mode2: $E000 → 8KB bank 6 (0x07)");
}

/* =========================================================================
 * test_prg_ram_6000 — $6000-$7FFF R/W via $5113
 * =========================================================================
 * Write-protect must be unlocked.  $5113 selects the PRG-RAM bank.
 */
static void test_prg_ram_6000(void) {
    printf("\n[test_prg_ram_6000]\n");
    struct mapper *map = make_mapper(2, 1);

    unlock_ram(map);

    // Default bank 0 of PRG-RAM.
    mapper_005_cpu_write(map, 0x6100, 0xAB);
    ASSERT(mapper_005_cpu_read(map, 0x6100) == 0xAB,
           "PRG-RAM bank 0: write then read back");

    // Switch to bank 1 via $5113.
    mapper_005_cpu_write(map, 0x5113, 0x01);
    mapper_005_cpu_write(map, 0x6100, 0xCD);
    ASSERT(mapper_005_cpu_read(map, 0x6100) == 0xCD,
           "PRG-RAM bank 1: write then read back");

    // Switch back to bank 0 — original value should still be there.
    mapper_005_cpu_write(map, 0x5113, 0x00);
    ASSERT(mapper_005_cpu_read(map, 0x6100) == 0xAB,
           "PRG-RAM bank 0 after switch: original value preserved");
}

/* =========================================================================
 * test_prg_ram_write_protect — locked by default; $5102/$5103 unlocks
 * =========================================================================
 */
static void test_prg_ram_write_protect(void) {
    printf("\n[test_prg_ram_write_protect]\n");
    struct mapper *map = make_mapper(2, 1);

    // Writes should be ignored when locked.
    mapper_005_cpu_write(map, 0x6000, 0x55);
    ASSERT(mapper_005_cpu_read(map, 0x6000) == 0x00,
           "PRG-RAM locked: write ignored (reads 0x00)");

    // Unlock.
    unlock_ram(map);
    mapper_005_cpu_write(map, 0x6000, 0x55);
    ASSERT(mapper_005_cpu_read(map, 0x6000) == 0x55,
           "PRG-RAM unlocked: write accepted");
}

/* =========================================================================
 * test_chr_mode3_1kb — 8 × 1 KB CHR windows (default mode 3)
 * =========================================================================
 * 2 × 8 KB CHR = 16 × 1 KB banks (filled 0x20-0x2F).
 * Assign each 1 KB window to a distinct bank and verify.
 */
static void test_chr_mode3_1kb(void) {
    printf("\n[test_chr_mode3_1kb]\n");
    struct mapper *map = make_mapper(2, 2);

    mapper_005_cpu_write(map, 0x5101, 3); // mode 3

    for (uint8_t w = 0; w < 8; w++)
        mapper_005_cpu_write(map, 0x5120 + w, w); // window w → CHR bank w

    int ok = 1;
    for (uint8_t w = 0; w < 8; w++) {
        uint16_t ppu_addr = (uint16_t)w * 0x0400;
        uint8_t expected = (uint8_t)(0x20 + w);
        uint8_t got = mapper_005_ppu_read(map, ppu_addr);
        if (got != expected) {
            printf("  FAIL: CHR mode3 window %d: expected 0x%02X, got 0x%02X\n",
                   w, expected, got);
            ok = 0;
        }
    }
    if (ok)
        printf("  PASS: all 8 CHR 1KB windows independently addressable\n");
    test_pass += ok;
    test_fail += !ok;
}

/* =========================================================================
 * test_chr_mode0_8kb — 8 KB window, set A register $5127
 * =========================================================================
 * 2 × 8 KB CHR = 16 × 1 KB banks.
 * Set $5127 = 8 (points at CHR 1KB bank 8, filled 0x28).
 * First read at PPU $0000 should return 0x28.
 */
static void test_chr_mode0_8kb(void) {
    printf("\n[test_chr_mode0_8kb]\n");
    struct mapper *map = make_mapper(2, 2);

    mapper_005_cpu_write(map, 0x5101, 0); // mode 0
    // In mode 0 the 8KB bank is derived from $5127 (index 7).
    // Raw value 8: bank[7:3] = 8>>3 = 1 → 8KB bank 1 = 1KB banks 8-15.
    mapper_005_cpu_write(map, 0x5127, 8);

    uint8_t v0 = mapper_005_ppu_read(map, 0x0000);
    uint8_t v1000 = mapper_005_ppu_read(map, 0x1000);
    // Both should be from the second 8KB block; first 1KB is bank 8 (0x28).
    ASSERT(v0 == 0x28, "CHR mode0: PPU $0000 → 1KB bank 8 (0x28)");
    ASSERT(v1000 == 0x2C, "CHR mode0: PPU $1000 → 1KB bank 12 (0x2C)");
}

/* =========================================================================
 * test_chr_mode1_4kb — 2 × 4 KB windows
 * =========================================================================
 * Set A:  $5123 (index 3) controls lower 4KB; $5127 (index 7) controls upper.
 */
static void test_chr_mode1_4kb(void) {
    printf("\n[test_chr_mode1_4kb]\n");
    struct mapper *map = make_mapper(2, 2);

    mapper_005_cpu_write(map, 0x5101, 1); // mode 1
    // Lower 4KB: $5123 = 4 → 4KB bank 1 (1KB banks 4-7, filled 0x24-0x27)
    mapper_005_cpu_write(map, 0x5123, 4);
    // Upper 4KB: $5127 = 12 → 4KB bank 3 (1KB banks 12-15, filled 0x2C-0x2F)
    mapper_005_cpu_write(map, 0x5127, 12);

    uint8_t vlo = mapper_005_ppu_read(map, 0x0000); // lower 4KB → bank 4 (0x24)
    uint8_t vhi =
        mapper_005_ppu_read(map, 0x1000); // upper 4KB → bank 12 (0x2C)
    ASSERT(vlo == 0x24, "CHR mode1: lower 4KB ($0000) → 1KB bank 4 (0x24)");
    ASSERT(vhi == 0x2C, "CHR mode1: upper 4KB ($1000) → 1KB bank 12 (0x2C)");
}

/* =========================================================================
 * test_chr_mode2_2kb — 4 × 2 KB windows
 * =========================================================================
 * Set A: odd indices (1,3,5,7) drive the four 2KB slots.
 * Slot 0 ($0000-$07FF): $5121 (index 1)
 * Slot 3 ($1800-$1FFF): $5127 (index 7)
 */
static void test_chr_mode2_2kb(void) {
    printf("\n[test_chr_mode2_2kb]\n");
    struct mapper *map = make_mapper(2, 2);

    mapper_005_cpu_write(map, 0x5101, 2); // mode 2
    // Slot 0: $5121 = 2 → 2KB bank 1 (1KB banks 2-3, filled 0x22/0x23)
    mapper_005_cpu_write(map, 0x5121, 2);
    // Slot 3: $5127 = 14 → 2KB bank 7 (1KB banks 14-15, filled 0x2E/0x2F)
    mapper_005_cpu_write(map, 0x5127, 14);

    uint8_t v0 = mapper_005_ppu_read(map, 0x0000); // slot 0 → bank 2 (0x22)
    uint8_t v1800 = mapper_005_ppu_read(map, 0x1800); // slot 3 → bank 14 (0x2E)
    ASSERT(v0 == 0x22, "CHR mode2: PPU $0000 → slot 0, 1KB bank 2 (0x22)");
    ASSERT(v1800 == 0x2E, "CHR mode2: PPU $1800 → slot 3, 1KB bank 14 (0x2E)");
}

/* =========================================================================
 * test_exram_rw — ExRAM R/W in mode 2 via $5C00-$5FFF
 * =========================================================================
 */
static void test_exram_rw(void) {
    printf("\n[test_exram_rw]\n");
    struct mapper *map = make_mapper(2, 1);

    // Mode 2 = general-purpose R/W RAM.
    mapper_005_cpu_write(map, 0x5104, 2);

    mapper_005_cpu_write(map, 0x5C00, 0x42);
    mapper_005_cpu_write(map, 0x5DFF, 0x99);

    ASSERT(mapper_005_cpu_read(map, 0x5C00) == 0x42,
           "ExRAM mode2: write $5C00 = 0x42, read back");
    ASSERT(mapper_005_cpu_read(map, 0x5DFF) == 0x99,
           "ExRAM mode2: write $5DFF = 0x99, read back");

    // Mode 3 = read-only: writes should be ignored.
    mapper_005_cpu_write(map, 0x5104, 3);
    mapper_005_cpu_write(map, 0x5C00, 0x11);
    ASSERT(mapper_005_cpu_read(map, 0x5C00) == 0x42,
           "ExRAM mode3 (R/O): write ignored, previous value preserved");
}

/* =========================================================================
 * test_fill_mode_nt — nametable fill-mode
 * =========================================================================
 * Set $5105 = 0xFF (all 4 nametables → fill-mode).
 * Set fill tile = 0xAA, fill attr = 0x02.
 * Read from tile area and attribute area.
 * (CIRAM pointer left NULL — fill-mode does not need CIRAM.)
 */
static void test_fill_mode_nt(void) {
    printf("\n[test_fill_mode_nt]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_005_cpu_write(map, 0x5105, 0xFF); // all 4 NTs → fill-mode
    mapper_005_cpu_write(map, 0x5106, 0xAA); // fill tile
    mapper_005_cpu_write(map, 0x5107, 0x02); // fill attr bits 1-0

    // Tile area (offset < $3C0 within the NT page)
    uint8_t tile = mapper_005_nt_read(map, 0x2000);
    ASSERT(tile == 0xAA, "fill-mode: tile area reads fill_tile (0xAA)");

    // Attribute area (offset >= $3C0)
    uint8_t attr = mapper_005_nt_read(map, 0x23C0);
    uint8_t expected_attr = 0x02 | (0x02 << 2) | (0x02 << 4) | (0x02 << 6);
    ASSERT(attr == expected_attr,
           "fill-mode: attr area returns fill_attr replicated to all 4 fields");

    // Nametable B ($2400) also fill-mode.
    uint8_t tile_b = mapper_005_nt_read(map, 0x2400);
    ASSERT(tile_b == 0xAA, "fill-mode: NT B ($2400) also reads fill_tile");
}

/* =========================================================================
 * test_ciram_nt_mapping — CIRAM bank 0 / 1 mapping via $5105
 * =========================================================================
 * Supply a 2 KB fake CIRAM buffer.
 * $5105 = 0x44 → vertical mirroring:
 *   NT 0 ($2000) → CIRAM bank 0 (bits 1-0 = 00)
 *   NT 1 ($2400) → CIRAM bank 1 (bits 3-2 = 01)
 *   NT 2 ($2800) → CIRAM bank 0 (bits 5-4 = 00)
 *   NT 3 ($2C00) → CIRAM bank 1 (bits 7-6 = 01)
 */
static void test_ciram_nt_mapping(void) {
    printf("\n[test_ciram_nt_mapping]\n");
    struct mapper *map = make_mapper(2, 1);

    // Provide a fake 2 KB CIRAM buffer.
    static uint8_t fake_ciram[0x800];
    memset(fake_ciram, 0, sizeof fake_ciram);
    fake_ciram[0x000] = 0x11; // bank 0 offset 0
    fake_ciram[0x001] = 0x22; // bank 0 offset 1
    fake_ciram[0x400] = 0x33; // bank 1 offset 0
    map->ciram = fake_ciram;

    // Vertical mirroring: $5105 = 0b01000100 = 0x44
    // NT0 bits[1:0]=00 → CIRAM 0; NT1 bits[3:2]=01 → CIRAM 1;
    // NT2 bits[5:4]=00 → CIRAM 0; NT3 bits[7:6]=01 → CIRAM 1
    mapper_005_cpu_write(map, 0x5105, 0x44);

    ASSERT(mapper_005_nt_read(map, 0x2000) == 0x11,
           "NT0 → CIRAM bank 0 offset 0 (0x11)");
    ASSERT(mapper_005_nt_read(map, 0x2001) == 0x22,
           "NT0 → CIRAM bank 0 offset 1 (0x22)");
    ASSERT(mapper_005_nt_read(map, 0x2400) == 0x33,
           "NT1 → CIRAM bank 1 offset 0 (0x33)");
    ASSERT(mapper_005_nt_read(map, 0x2800) == 0x11,
           "NT2 → CIRAM bank 0 offset 0 (mirrors NT0)");

    // Write through CIRAM bank 1 via NT3 ($2C00).
    mapper_005_nt_write(map, 0x2C00, 0x77);
    ASSERT(fake_ciram[0x400] == 0x77,
           "NT3 write → CIRAM bank 1 offset 0 updated to 0x77");
}

/* =========================================================================
 * test_scanline_irq_fires — IRQ fires when scanline matches latch
 * =========================================================================
 * Set latch = 3, enable IRQ, then call the scanline hook 3 times.
 * IRQ should fire on the 3rd call (scanline_count goes 0→1→2→3).
 */
static void test_scanline_irq_fires(void) {
    printf("\n[test_scanline_irq_fires]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_005_cpu_write(map, 0x5203, 3);    // IRQ latch = 3
    mapper_005_cpu_write(map, 0x5204, 0x80); // enable IRQ

    ASSERT(map->irq_pending == 0, "IRQ: not pending at start");

    mapper_005_scanline(map); // first call: in_frame = 1, count = 0
    ASSERT(map->irq_pending == 0, "IRQ: not pending after 1st scanline");

    mapper_005_scanline(map); // count = 1
    ASSERT(map->irq_pending == 0, "IRQ: not pending after 2nd scanline");

    mapper_005_scanline(map); // count = 2
    ASSERT(map->irq_pending == 0, "IRQ: not pending after 3rd scanline");

    mapper_005_scanline(map); // count = 3 → fires
    ASSERT(map->irq_pending == 1,
           "IRQ: pending after 4th scanline (count==latch==3)");
}

/* =========================================================================
 * test_scanline_irq_disabled — no IRQ when disabled
 * =========================================================================
 */
static void test_scanline_irq_disabled(void) {
    printf("\n[test_scanline_irq_disabled]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_005_cpu_write(map, 0x5203, 2);    // latch = 2
    mapper_005_cpu_write(map, 0x5204, 0x00); // IRQ disabled

    for (int i = 0; i < 5; i++)
        mapper_005_scanline(map);

    ASSERT(map->irq_pending == 0,
           "IRQ: never fires when IRQ enable bit is clear");
}

/* =========================================================================
 * test_scanline_irq_cleared_by_read — $5204 read clears pending flag
 * =========================================================================
 */
static void test_scanline_irq_cleared_by_read(void) {
    printf("\n[test_scanline_irq_cleared_by_read]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_005_cpu_write(map, 0x5203, 1);    // latch = 1
    mapper_005_cpu_write(map, 0x5204, 0x80); // enable

    mapper_005_scanline(map); // count = 0, in_frame set
    mapper_005_scanline(map); // count = 1 → fires

    ASSERT(map->irq_pending == 1, "IRQ: pending before read");

    // $5204 read should return the pending bit and clear it.
    uint8_t status = mapper_005_cpu_read(map, 0x5204);
    ASSERT((status & 0x80) != 0, "$5204 read: bit 7 = irq_pending was set");
    ASSERT(map->irq_pending == 0, "IRQ: cleared after $5204 read");
}

/* =========================================================================
 * test_multiply — 8×8 unsigned multiply unit
 * =========================================================================
 * Write 0x12 to $5205 (factor A) and 0x34 to $5206 (factor B).
 * 0x12 = 18, 0x34 = 52.  18 × 52 = 936 = 0x03A8.
 * $5205 read = 0xA8 (low byte), $5206 read = 0x03 (high byte).
 */
static void test_multiply(void) {
    printf("\n[test_multiply]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_005_cpu_write(map, 0x5205, 0x12);
    mapper_005_cpu_write(map, 0x5206, 0x34);

    uint8_t lo = mapper_005_cpu_read(map, 0x5205);
    uint8_t hi = mapper_005_cpu_read(map, 0x5206);
    uint16_t product = (uint16_t)((hi << 8) | lo);

    ASSERT(product == (uint16_t)(0x12 * 0x34),
           "multiply: 0x12 × 0x34 = 936 (0x03A8)");
    ASSERT(lo == 0xA8, "multiply: low byte = 0xA8");
    ASSERT(hi == 0x03, "multiply: high byte = 0x03");
}

/* =========================================================================
 * test_chr_bankset_8x8_mode — in 8×8 mode all fetches use the last-written set
 * =========================================================================
 * The last-written set is tracked by last_chr_set.  Writing any set-A register
 * sets last_chr_set=0 (→ use A); writing any set-B register sets it=1 (→ use
 * B). ppu_sprite_16 = 0 throughout.
 *
 * Setup (CHR mode 3, 1 CHR-ROM bank = 8 × 1 KB):
 *   Set-A slot 0 ($5120) = 3 → 1 KB bank 3 → filled 0x23
 *   Set-B slot 0 ($5128) = 5 → 1 KB bank 5 → filled 0x25
 *
 * After writing set B last: all reads → 0x25.
 * After writing set A last: all reads → 0x23.
 */
static void test_chr_bankset_8x8_mode(void) {
    printf("\n[test_chr_bankset_8x8_mode]\n");
    struct mapper *map = make_mapper(2, 1); // 1 CHR bank = 8 × 1 KB

    // CHR mode 3 (1 KB windows) is the default.
    mapper_005_cpu_write(map, 0x5120, 3); // set A slot 0 → bank 3 → 0x23
    mapper_005_cpu_write(map, 0x5128, 5); // set B slot 0 → bank 5 → 0x25
    // last write was set B → last_chr_set = 1 → BG uses set B in 8×8 mode

    map->ppu_sprite_16 = 0; // 8×8 mode
    map->ppu_sprite_fetch = 0;
    uint8_t bg_after_b = mapper_005_ppu_read(map, 0x0000);
    ASSERT(bg_after_b == 0x25,
           "CHR 8x8: after writing set-B last, reads return set-B data (0x25)");

    // Now write set A last
    mapper_005_cpu_write(map, 0x5120, 3); // set A slot 0 again
    map->ppu_sprite_fetch = 0;
    uint8_t bg_after_a = mapper_005_ppu_read(map, 0x0000);
    ASSERT(bg_after_a == 0x23,
           "CHR 8x8: after writing set-A last, reads return set-A data (0x23)");

    // In 8×8 mode, ppu_sprite_fetch = 1 also uses last-written set (set A now)
    map->ppu_sprite_fetch = 1;
    uint8_t sp_after_a = mapper_005_ppu_read(map, 0x0000);
    ASSERT(sp_after_a == 0x23,
           "CHR 8x8: sprite fetch also uses last-written set (set-A = 0x23)");
}

/* =========================================================================
 * test_chr_bankset_8x16_mode — 8×16 mode: sprite→A, BG→B (strict split)
 * =========================================================================
 * With ppu_sprite_16 = 1:
 *   ppu_sprite_fetch = 1 → always use set A (regardless of last_chr_set)
 *   ppu_sprite_fetch = 0 → always use set B (regardless of last_chr_set)
 *
 * Setup (CHR mode 3, 1 CHR bank = 8 × 1 KB):
 *   Set-A slot 0 ($5120) = 2 → bank 2 → 0x22
 *   Set-B slot 0 ($5128) = 6 → bank 6 → 0x26
 *
 * Writing set-A last (last_chr_set=0), but BG must still use set B.
 * This was the bug: last_chr_set=0 → use_A was 1, so BG fetched sprite data.
 */
static void test_chr_bankset_8x16_mode(void) {
    printf("\n[test_chr_bankset_8x16_mode]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_005_cpu_write(map, 0x5120, 2); // set A slot 0 → bank 2 → 0x22
    mapper_005_cpu_write(map, 0x5128, 6); // set B slot 0 → bank 6 → 0x26
    mapper_005_cpu_write(map, 0x5120,
                         2); // write set A again → last_chr_set = 0

    map->ppu_sprite_16 = 1; // 8×16 mode

    // Sprite fetch must use set A
    map->ppu_sprite_fetch = 1;
    uint8_t sp = mapper_005_ppu_read(map, 0x0000);
    ASSERT(sp == 0x22,
           "CHR 8x16: sprite fetch uses set-A (0x22) even when last_chr_set=0");

    // BG fetch must use set B (NOT set A despite last_chr_set=0)
    map->ppu_sprite_fetch = 0;
    uint8_t bg = mapper_005_ppu_read(map, 0x0000);
    ASSERT(bg == 0x26,
           "CHR 8x16: BG fetch uses set-B (0x26) regardless of last_chr_set");

    // Flip last write to set B; sprite must still use A
    mapper_005_cpu_write(map, 0x5128, 6); // last_chr_set = 1 now
    map->ppu_sprite_fetch = 1;
    uint8_t sp2 = mapper_005_ppu_read(map, 0x0000);
    ASSERT(sp2 == 0x22,
           "CHR 8x16: sprite fetch still uses set-A after set-B write");

    map->ppu_sprite_fetch = 0;
    uint8_t bg2 = mapper_005_ppu_read(map, 0x0000);
    ASSERT(bg2 == 0x26,
           "CHR 8x16: BG fetch still uses set-B after set-B write");
}

/* =========================================================================
 * test_exram_mode1_attr_synthesis — ExRAM mode 1 synthesizes attribute bytes
 * =========================================================================
 * In ExRAM mode 1 the NT attribute read is intercepted and a byte is built
 * from ExRAM palette bits, giving per-2×2-tile palette control.
 *
 * Attribute index 0 covers tiles (0,0), (1,0), (0,1), (1,1) mapped to
 * ExRAM[0], [2], [32], [34] (2×2 sub-region indices).
 *
 * An attribute byte covers a 4×4 tile region.  Each 2×2 sub-region has one
 * representative tile in ExRAM:
 *   TL: tile (base_col+0, base_row+0)  =  ExRAM[base_row*32 + base_col]
 *   TR: tile (base_col+2, base_row+0)  =  ExRAM[base_row*32 + base_col+2]
 *   BL: tile (base_col+0, base_row+2)  =  ExRAM[(base_row+2)*32 + base_col]
 *   BR: tile (base_col+2, base_row+2)  =  ExRAM[(base_row+2)*32 + base_col+2]
 *
 * AT byte 0 (at_col=0, at_row=0 → base_col=0, base_row=0):
 *   TL: ExRAM[0]  = 0x01 → palette 1
 *   TR: ExRAM[2]  = 0x02 → palette 2
 *   BL: ExRAM[64] = 0x03 → palette 3   (= 2*32 + 0)
 *   BR: ExRAM[66] = 0x00 → palette 0   (= 2*32 + 2)
 *
 * Expected attribute byte: 1 | (2<<2) | (3<<4) | (0<<6) = 0x31
 */
static void test_exram_mode1_attr_synthesis(void) {
    printf("\n[test_exram_mode1_attr_synthesis]\n");
    struct mapper *map = make_mapper(2, 1);

    // Enable ExRAM mode 1
    mapper_005_cpu_write(map, 0x5104, 0x01);

    // Write ExRAM palette values for the four 2×2 sub-regions of AT byte 0.
    // AT byte 0: at_col=0, at_row=0 → base_col=0, base_row=0
    //   TL tile (0,0) = ExRAM[0],  TR tile (2,0) = ExRAM[2]
    //   BL tile (0,2) = ExRAM[64], BR tile (2,2) = ExRAM[66]
    mapper_005_cpu_write(map, 0x5C00 + 0, 0x01); // TL = 1
    mapper_005_cpu_write(map, 0x5C00 + 2, 0x02); // TR = 2
    mapper_005_cpu_write(map, 0x5C00 + 64,
                         0x03); // BL = 3  (row 2 × 32 + col 0)
    mapper_005_cpu_write(map, 0x5C00 + 66,
                         0x00); // BR = 0  (row 2 × 32 + col 2)

    // Attribute byte 0 in NT 0 is at offset $3C0 → address $23C0.
    // With exram_mode=1 the synthesized byte must be returned regardless of
    // $5105 routing (nt_mapping=0xFF fill-mode by default).
    uint8_t attr = mapper_005_nt_read(map, 0x23C0);
    uint8_t expected = (uint8_t)(1 | (2 << 2) | (3 << 4) | (0 << 6)); // = 0x31
    ASSERT(attr == expected,
           "ExRAM mode1: AT byte 0 synthesized from ExRAM palette bits (0x31)");

    // Second attribute byte (index 1): at_col=1, at_row=0 → base_col=4,
    // base_row=0
    //   TL tile (4,0) = ExRAM[4],  TR tile (6,0) = ExRAM[6]
    //   BL tile (4,2) = ExRAM[68], BR tile (6,2) = ExRAM[70]
    mapper_005_cpu_write(map, 0x5C00 + 4, 0x02);
    mapper_005_cpu_write(map, 0x5C00 + 6, 0x03);
    mapper_005_cpu_write(map, 0x5C00 + 68, 0x01); // 2*32+4 = 68
    mapper_005_cpu_write(map, 0x5C00 + 70, 0x02); // 2*32+6 = 70
    uint8_t attr1 = mapper_005_nt_read(map, 0x23C1);
    uint8_t expected1 = (uint8_t)(2 | (3 << 2) | (1 << 4) | (2 << 6));
    ASSERT(attr1 == expected1,
           "ExRAM mode1: AT byte 1 synthesized correctly from ExRAM");

    // In normal mode (exram_mode=0) the AT read goes through CIRAM routing
    // unchanged — verify the synthesis does NOT fire outside mode 1.
    mapper_005_cpu_write(map, 0x5104, 0x00); // mode 0
    // With ciram=NULL and slot=3 (fill-mode default), return fill_attr byte.
    // fill_attr = 0 → fill byte = 0x00.
    uint8_t attr_mode0 = mapper_005_nt_read(map, 0x23C0);
    // fill_attr=0 replicated 4× = 0x00
    ASSERT(attr_mode0 == 0x00,
           "ExRAM mode0: AT byte not synthesized — fill-mode attr returned");
}

/* =========================================================================
 * test_exram_mode1_chr_bank_extension — ExRAM bits 7-6 extend CHR bank to 10b
 * =========================================================================
 * With 3 CHR-ROM banks (24 × 1 KB banks, numbered 0-23):
 *   bank j filled with (0x20 + j)
 *
 * Set-B slot 0 ($5128) = 0 → base bank = 0 → data normally = 0x20.
 *
 * With exram_latch = 0x40 (bits 7-6 = 01):
 *   extended_bank = (1 << 8) | 0 = 256 → 256 % 24 = 16 → data = 0x30.
 *
 * With exram_latch = 0x00 (bits 7-6 = 00):
 *   extended_bank = 0 → 0 % 24 = 0 → data = 0x20.
 *
 * The latch is updated by calling nt_read with a tile address; nt_mapping
 * defaults to 0xFF (all fill-mode) so the return value is discarded.
 */
static void test_exram_mode1_chr_bank_extension(void) {
    printf("\n[test_exram_mode1_chr_bank_extension]\n");
    struct mapper *map = make_mapper(2, 3); // 3 CHR banks = 24 × 1 KB banks

    // Enable ExRAM mode 1
    mapper_005_cpu_write(map, 0x5104, 0x01);

    // Set-B bank register 0 = bank 0 (filled 0x20)
    mapper_005_cpu_write(map, 0x5128, 0x00);

    // 8×16 mode, BG fetch (must use set B)
    map->ppu_sprite_16 = 1;
    map->ppu_sprite_fetch = 0;

    // ── Without ExRAM extension (latch bits 7-6 = 00) ────────────────────
    // Write 0x00 into ExRAM[0] and trigger latch update.
    mapper_005_cpu_write(map, 0x5C00 + 0, 0x00);
    mapper_005_nt_read(map, 0x2000); // tile at NT offset 0 → latch = 0x00
    uint8_t v_no_ext = mapper_005_ppu_read(map, 0x0000);
    ASSERT(v_no_ext == 0x20,
           "ExRAM mode1 CHR ext: latch=0x00 → bank 0 → 0x20 (no extension)");

    // ── With ExRAM extension (latch bits 7-6 = 01, high = 256) ──────────
    // Write 0x40 into ExRAM[0] (bits 7-6 = 01).
    mapper_005_cpu_write(map, 0x5C00 + 0, 0x40);
    mapper_005_nt_read(map, 0x2000); // latch = 0x40
    uint8_t v_ext = mapper_005_ppu_read(map, 0x0000);
    // extended = (1<<8)|0 = 256; 256 % 24 = 16; CHR bank 16 → 0x20+16 = 0x30
    ASSERT(v_ext == 0x30,
           "ExRAM mode1 CHR ext: latch=0x40 → bank 256%24=16 → 0x30");

    // ── Extension only applies to BG fetches (set B) in 8×16 mode ────────
    // Sprite fetch must still use set A, unaffected by exram_latch.
    mapper_005_cpu_write(map, 0x5120, 0x01); // set A slot 0 → bank 1 → 0x21
    map->ppu_sprite_fetch = 1;
    uint8_t sp = mapper_005_ppu_read(map, 0x0000);
    ASSERT(
        sp == 0x21,
        "ExRAM mode1 CHR ext: sprite fetch unaffected by exram_latch → 0x21");
}

/* =========================================================================
 * main
 * =========================================================================
 */
int main(void) {
    printf("=== Mapper 005 (MMC5 / ExROM) unit tests ===\n");

    test_prg_mode3_banks();
    test_prg_mode0_32kb();
    test_prg_mode1_16kb();
    test_prg_mode2_mixed();
    test_prg_ram_6000();
    test_prg_ram_write_protect();
    test_chr_mode3_1kb();
    test_chr_mode0_8kb();
    test_chr_mode1_4kb();
    test_chr_mode2_2kb();
    test_exram_rw();
    test_fill_mode_nt();
    test_ciram_nt_mapping();
    test_scanline_irq_fires();
    test_scanline_irq_disabled();
    test_scanline_irq_cleared_by_read();
    test_multiply();
    test_chr_bankset_8x8_mode();
    test_chr_bankset_8x16_mode();
    test_exram_mode1_attr_synthesis();
    test_exram_mode1_chr_bank_extension();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
