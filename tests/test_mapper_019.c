/*
 * test_mapper_019.c
 *
 * Unit tests for Mapper 019 — Namco 163 (N163).
 *
 * Coverage:
 *   - CHR 1KB banking ($8000-$BFFF registers, all 8 slots)
 *   - PRG 8KB banking ($E000/$E800/$F000 registers; fixed last bank at $E000)
 *   - Mirroring register ($F800): H, V, single-lo
 *   - IRQ counter: enable, increment, fire at $7FFF wrap, disable/clear
 *   - Internal RAM: read/write at $4800-$4FFF (addr & 0x7F indexing)
 *   - CHR-RAM windows (chr_ram_lo/hi bits from $E800)
 *   - Power-up state: PRG banks default to 0, irq disabled
 *   - IRQ pending cleared on counter register write
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_019.h"
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

/* ---------------------------------------------------------------------------
 * Helper: create a cartridge with num_prg 16KB PRG banks and num_chr 8KB
 * CHR banks.
 *   PRG 8KB bank i   → filled with byte (i + 1)
 *   CHR 1KB bank j   → filled with byte (0x40 + j)
 * ------------------------------------------------------------------------- */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = num_chr;
    hdr->flags6.flagss = 0; /* horizontal mirroring default */
    cart->hdr = hdr;
    cart->mapper_id = 19;
    cart->mapper_number = 19;

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
            memset(cart->chr_rom + j * 0x0400, (uint8_t)(0x40 + j), 0x0400);
    }

    return cart;
}

static struct mapper g_map;

static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr);
    memset(&g_map, 0, sizeof g_map);
    g_map.cartridge   = cart;
    g_map.mapper_id   = 19;
    g_map.num_prg_rom = num_prg;
    g_map.num_chr_rom = num_chr;
    g_map.mirroring   = MIRROR_HORIZONTAL;
    g_map.cpu_read    = mapper_019_cpu_read;
    g_map.cpu_write   = mapper_019_cpu_write;
    g_map.ppu_read    = mapper_019_ppu_read;
    g_map.ppu_write   = mapper_019_ppu_write;
    g_map.clock       = mapper_019_cpu_cycle;
    mapper_019_init(&g_map);
    return &g_map;
}

/* -------------------------------------------------------------------------
 * test_power_up_prg
 *
 * At power-up all prg_bank[] = 0, so $8000/$A000/$C000 all map to 8KB bank
 * 0 (filled 0x01).  $E000-$FFFF is always the last bank.
 * With 4 × 16KB = 8 × 8KB banks: last bank = bank 7 (filled 0x08).
 * ------------------------------------------------------------------------- */
static void test_power_up_prg(void) {
    printf("\n[test_power_up_prg]\n");
    struct mapper *map = make_mapper(4, 1);

    uint8_t v8 = mapper_019_cpu_read(map, 0x8000);
    ASSERT(v8 == 1, "power-up: $8000 = bank 0 (filled 0x01)");

    uint8_t va = mapper_019_cpu_read(map, 0xA000);
    ASSERT(va == 1, "power-up: $A000 = bank 0 (filled 0x01)");

    uint8_t vc = mapper_019_cpu_read(map, 0xC000);
    ASSERT(vc == 1, "power-up: $C000 = bank 0 (filled 0x01)");

    /* Fixed last bank at $E000 (bank 7, filled 0x08). */
    uint8_t ve = mapper_019_cpu_read(map, 0xE000);
    ASSERT(ve == 8, "power-up: $E000 = last bank 7 (filled 0x08)");
}

/* -------------------------------------------------------------------------
 * test_prg_bank_switch
 *
 * Write PRG bank registers and verify reads from correct banks.
 * PRG bank 0 ($8000-$9FFF) ← register $E000; bank 1 ($A000-$BFFF) ← $E800;
 * bank 2 ($C000-$DFFF) ← $F000.
 * ------------------------------------------------------------------------- */
static void test_prg_bank_switch(void) {
    printf("\n[test_prg_bank_switch]\n");
    struct mapper *map = make_mapper(4, 1); /* 8 × 8KB PRG banks */

    /* Set bank 0 = 2 → $8000 reads bank 2 (filled 0x03). */
    mapper_019_cpu_write(map, 0xE000, 2);
    uint8_t v8 = mapper_019_cpu_read(map, 0x8000);
    ASSERT(v8 == 3, "PRG bank 0 = 2: $8000 reads 0x03");

    /* Set bank 1 = 5 → $A000 reads bank 5 (filled 0x06). */
    mapper_019_cpu_write(map, 0xE800, 5);
    uint8_t va = mapper_019_cpu_read(map, 0xA000);
    ASSERT(va == 6, "PRG bank 1 = 5: $A000 reads 0x06");

    /* Set bank 2 = 3 → $C000 reads bank 3 (filled 0x04). */
    mapper_019_cpu_write(map, 0xF000, 3);
    uint8_t vc = mapper_019_cpu_read(map, 0xC000);
    ASSERT(vc == 4, "PRG bank 2 = 3: $C000 reads 0x04");

    /* Fixed last bank at $E000 unchanged (bank 7, filled 0x08). */
    uint8_t ve = mapper_019_cpu_read(map, 0xE000);
    ASSERT(ve == 8, "PRG fixed: $E000 still reads last bank (filled 0x08)");
}

/* -------------------------------------------------------------------------
 * test_chr_banking
 *
 * CHR bank registers at $8000-$BFFF (step $800 per slot).
 * Each slot controls one 1KB CHR window.  Write different bank indices to
 * slots 0 and 4 (PPU $0000 and $1000) and verify ppu_read returns the
 * correct fill byte.
 * ------------------------------------------------------------------------- */
static void test_chr_banking(void) {
    printf("\n[test_chr_banking]\n");
    /* 2 × 8KB CHR = 16 × 1KB banks.  Bank j filled with 0x40+j. */
    struct mapper *map = make_mapper(2, 2);

    /* Slot 0 ($8000): select CHR 1KB bank 3 → PPU $0000 reads 0x43. */
    mapper_019_cpu_write(map, 0x8000, 3);
    uint8_t v0 = mapper_019_ppu_read(map, 0x0000);
    ASSERT(v0 == 0x43, "CHR slot 0 = 3: PPU $0000 reads 0x43");

    /* Slot 1 ($8800): select CHR 1KB bank 7 → PPU $0400 reads 0x47. */
    mapper_019_cpu_write(map, 0x8800, 7);
    uint8_t v4 = mapper_019_ppu_read(map, 0x0400);
    ASSERT(v4 == 0x47, "CHR slot 1 = 7: PPU $0400 reads 0x47");

    /* Slot 4 ($A000): select CHR 1KB bank 10 → PPU $1000 reads 0x4A. */
    mapper_019_cpu_write(map, 0xA000, 10);
    uint8_t v10 = mapper_019_ppu_read(map, 0x1000);
    ASSERT(v10 == 0x4A, "CHR slot 4 = 10: PPU $1000 reads 0x4A");

    /* Slot 7 ($B800): select CHR 1KB bank 15 → PPU $1C00 reads 0x4F. */
    mapper_019_cpu_write(map, 0xB800, 15);
    uint8_t v1c = mapper_019_ppu_read(map, 0x1C00);
    ASSERT(v1c == 0x4F, "CHR slot 7 = 15: PPU $1C00 reads 0x4F");
}

/* -------------------------------------------------------------------------
 * test_mirroring
 *
 * $F800 bits 1-0: 00/11 = single-lo, 01 = H, 10 = V.
 * ------------------------------------------------------------------------- */
static void test_mirroring(void) {
    printf("\n[test_mirroring]\n");
    struct mapper *map = make_mapper(2, 1);

    mapper_019_cpu_write(map, 0xF800, 0x01); /* horizontal */
    ASSERT(map->mirroring == MIRROR_HORIZONTAL, "$F800=01 → horizontal");

    mapper_019_cpu_write(map, 0xF800, 0x02); /* vertical */
    ASSERT(map->mirroring == MIRROR_VERTICAL, "$F800=02 → vertical");

    mapper_019_cpu_write(map, 0xF800, 0x00); /* single-lo */
    ASSERT(map->mirroring == MIRROR_SINGLE_LO, "$F800=00 → single-lo");

    mapper_019_cpu_write(map, 0xF800, 0x03); /* single-lo (alt encoding) */
    ASSERT(map->mirroring == MIRROR_SINGLE_LO, "$F800=03 → single-lo");
}

/* -------------------------------------------------------------------------
 * test_internal_ram
 *
 * $4800-$4FFF: 128 bytes of Namco 163 internal RAM, addressed as addr & 0x7F.
 * Write to two different addresses that alias to the same slot, and verify
 * the aliasing and read-back.
 * ------------------------------------------------------------------------- */
static void test_internal_ram(void) {
    printf("\n[test_internal_ram]\n");
    struct mapper *map = make_mapper(2, 1);

    /* Write 0xAB to addr $4810 → index 0x10. */
    mapper_019_cpu_write(map, 0x4810, 0xAB);
    uint8_t v = mapper_019_cpu_read(map, 0x4810);
    ASSERT(v == 0xAB, "internal RAM: $4810 write/read-back 0xAB");

    /* Write 0x55 to addr $4890 → index 0x10 (same due to & 0x7F). */
    mapper_019_cpu_write(map, 0x4890, 0x55);
    uint8_t v2 = mapper_019_cpu_read(map, 0x4810);
    ASSERT(v2 == 0x55, "internal RAM: $4890 aliases to $4810 (& 0x7F)");

    /* Different slot: $4850 → index 0x50. */
    mapper_019_cpu_write(map, 0x4850, 0x77);
    uint8_t v3 = mapper_019_cpu_read(map, 0x4850);
    ASSERT(v3 == 0x77, "internal RAM: $4850 write/read-back 0x77");
}

/* -------------------------------------------------------------------------
 * test_irq_counter_basic
 *
 * Load counter with 2 ($5000 low, $5800 high+enable), then call
 * mapper_019_cpu_cycle() enough times to overflow.  The counter starts at 2
 * and must increment to 0x7FFF before firing (0x7FFF - 2 + 1 increments to
 * reach 0x7FFF, then the next clock wraps to 0 and fires).
 *
 * For a fast test we preload the counter near the top.
 * ------------------------------------------------------------------------- */
static void test_irq_counter_basic(void) {
    printf("\n[test_irq_counter_basic]\n");
    struct mapper *map = make_mapper(2, 1);

    /* Preload counter = 0x7FFD (low=0xFD, high=0x7F without enable bit). */
    mapper_019_cpu_write(map, 0x5000, 0xFD); /* low byte  */
    mapper_019_cpu_write(map, 0x5800, 0xFF); /* high 7 bits = 0x7F; bit 7 = enable */

    ASSERT(map->irq_pending == 0, "IRQ: not pending at start");

    /* Tick once — counter 0x7FFD → 0x7FFE; no IRQ. */
    mapper_019_cpu_cycle(map);
    ASSERT(map->irq_pending == 0, "IRQ: not pending after tick 1 (0x7FFE)");

    /* Tick again — counter 0x7FFE → 0x7FFF; no IRQ yet. */
    mapper_019_cpu_cycle(map);
    ASSERT(map->irq_pending == 0, "IRQ: not pending after tick 2 (0x7FFF)");

    /* Tick again — counter was 0x7FFF (max), wraps to 0, IRQ fires. */
    mapper_019_cpu_cycle(map);
    ASSERT(map->irq_pending == 1, "IRQ: pending after tick 3 (wrap 0x7FFF→0)");
    ASSERT(map->irq_pending == 1, "IRQ: map->irq_pending also set");
}

/* -------------------------------------------------------------------------
 * test_irq_disable_no_fire
 *
 * With IRQ enable bit clear, counter ticks but never fires.
 * ------------------------------------------------------------------------- */
static void test_irq_disable_no_fire(void) {
    printf("\n[test_irq_disable_no_fire]\n");
    struct mapper *map = make_mapper(2, 1);

    /* Preload near top but disable IRQ (bit 7 of $5800 = 0). */
    mapper_019_cpu_write(map, 0x5000, 0xFD);
    mapper_019_cpu_write(map, 0x5800, 0x7F); /* high=0x7F, enable=0 */

    mapper_019_cpu_cycle(map);
    mapper_019_cpu_cycle(map);
    mapper_019_cpu_cycle(map); /* would wrap */

    ASSERT(map->irq_pending == 0, "IRQ disabled: no IRQ fires even at wrap");
}

/* -------------------------------------------------------------------------
 * test_irq_counter_write_clears_pending
 *
 * Writing to the IRQ counter registers ($5000 or $5800) while IRQ is pending
 * should clear irq_pending.
 * ------------------------------------------------------------------------- */
static void test_irq_counter_write_clears_pending(void) {
    printf("\n[test_irq_counter_write_clears_pending]\n");
    struct mapper *map = make_mapper(2, 1);

    /* Fire IRQ by preloading 0x7FFF and ticking once. */
    mapper_019_cpu_write(map, 0x5000, 0xFF);
    mapper_019_cpu_write(map, 0x5800, 0xFF); /* enable + high=0x7F */
    mapper_019_cpu_cycle(map); /* 0x7FFF → wrap, IRQ fires */
    ASSERT(map->irq_pending == 1, "IRQ fires before clear");

    /* Write to low byte register — should clear pending. */
    mapper_019_cpu_write(map, 0x5000, 0x00);
    ASSERT(map->irq_pending == 0, "IRQ pending cleared by $5000 write");

    /* Re-trigger. */
    mapper_019_cpu_write(map, 0x5000, 0xFF);
    mapper_019_cpu_write(map, 0x5800, 0xFF);
    mapper_019_cpu_cycle(map);
    ASSERT(map->irq_pending == 1, "IRQ fires again");

    /* Write to high byte register — should clear pending. */
    mapper_019_cpu_write(map, 0x5800, 0xFF);
    ASSERT(map->irq_pending == 0, "IRQ pending cleared by $5800 write");
}

/* -------------------------------------------------------------------------
 * test_chr_ram_windows
 *
 * $E800 bit 6 enables CHR-RAM at $0000-$0FFF (lower 4KB); bit 7 enables
 * CHR-RAM at $1000-$1FFF (upper 4KB).  Writes should go to internal CHR-RAM
 * and reads return the written value regardless of chr_bank registers.
 * ------------------------------------------------------------------------- */
static void test_chr_ram_windows(void) {
    printf("\n[test_chr_ram_windows]\n");
    /* Cart with 0 CHR-ROM banks (all CHR-RAM). */
    struct mapper *map = make_mapper(2, 0);

    /* Enable CHR-RAM on lower half (bit 6) via $E800. */
    mapper_019_cpu_write(map, 0xE800, 0x40); /* chr_ram_lo = 1, chr_ram_hi = 0 */

    /* Write to PPU $0100 — should land in CHR-RAM. */
    mapper_019_ppu_write(map, 0x0100, 0xBE);
    uint8_t v = mapper_019_ppu_read(map, 0x0100);
    ASSERT(v == 0xBE, "CHR-RAM lo: PPU write/read at $0100 = 0xBE");

    /* Upper half is NOT CHR-RAM; reads should return 0 (no CHR-ROM). */
    uint8_t v2 = mapper_019_ppu_read(map, 0x1100);
    ASSERT(v2 == 0, "CHR-RAM hi disabled: PPU $1100 reads 0 (no CHR-ROM)");

    /* Enable CHR-RAM on upper half too (bit 7). */
    mapper_019_cpu_write(map, 0xE800, 0xC0); /* bits 6+7 set */
    mapper_019_ppu_write(map, 0x1200, 0xEF);
    uint8_t v3 = mapper_019_ppu_read(map, 0x1200);
    ASSERT(v3 == 0xEF, "CHR-RAM hi: PPU write/read at $1200 = 0xEF");
}

/* -------------------------------------------------------------------------
 * test_irq_counter_read
 *
 * Reads from $5000 and $5800 return the current counter value.
 * ------------------------------------------------------------------------- */
static void test_irq_counter_read(void) {
    printf("\n[test_irq_counter_read]\n");
    struct mapper *map = make_mapper(2, 1);

    /* Set counter to 0x1234 with enable=1. */
    mapper_019_cpu_write(map, 0x5000, 0x34); /* low */
    mapper_019_cpu_write(map, 0x5800, 0x92); /* high 7 bits = 0x12, enable = 1 */

    uint8_t lo = mapper_019_cpu_read(map, 0x5000);
    ASSERT(lo == 0x34, "IRQ counter read: low byte = 0x34");

    uint8_t hi = mapper_019_cpu_read(map, 0x5800);
    /* bit 7 = enable (1), bits 6-0 = high 7 bits of counter (0x12). */
    ASSERT((hi & 0x7F) == 0x12, "IRQ counter read: high 7 bits = 0x12");
    ASSERT((hi >> 7) == 1,      "IRQ counter read: enable bit = 1");
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void) {
    printf("=== Mapper 019 (Namco 163) unit tests ===\n");

    test_power_up_prg();
    test_prg_bank_switch();
    test_chr_banking();
    test_mirroring();
    test_internal_ram();
    test_irq_counter_basic();
    test_irq_disable_no_fire();
    test_irq_counter_write_clears_pending();
    test_chr_ram_windows();
    test_irq_counter_read();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
