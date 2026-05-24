/*
 * test_mapper_001.c
 *
 * Unit tests for the MMC1 (Mapper 001) implementation.
 *
 * Tests cover all four PRG bank modes, both CHR bank modes, dynamic mirroring,
 * PRG-RAM enable/disable, and the shift register reset mechanism.
 *
 * We construct a minimal nes_cartridge and mapper in-place; no SDL2 or full
 * emulator is needed.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mapper.h"
#include "mapper_001.h"
#include "cartridge.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

/* Write one bit to the MMC1 serial interface. */
static void serial_write(struct mapper *map, uint16_t reg_addr, uint8_t value) {
    for (int bit = 0; bit < 5; bit++) {
        uint8_t byte = (value >> bit) & 0x01;
        mapper_001_cpu_write(map, reg_addr, byte);
    }
}

/* Build a minimal cartridge with num_prg 16KB PRG-ROM banks and num_chr 8KB
 * CHR-ROM banks.  PRG bank i is filled with byte (i + 1); CHR bank i is
 * filled with byte (0x10 + i). */
static struct nes_cartridge *make_cart(uint8_t num_prg, uint8_t num_chr,
                                       int chr_ram) {
    struct nes_cartridge *cart = calloc(1, sizeof *cart);
    struct nes_cartridge_hdr *hdr = calloc(1, sizeof *hdr);
    hdr->prg_rom_size = num_prg;
    hdr->chr_rom_size = num_chr;
    cart->hdr = hdr;
    cart->mapper_id = 1;

    size_t prg_len = (size_t)num_prg * 0x4000;
    cart->prg_rom = calloc(1, prg_len);
    cart->prg_rom_len = prg_len;
    for (uint8_t i = 0; i < num_prg; i++)
        memset(cart->prg_rom + i * 0x4000, i + 1, 0x4000);

    size_t chr_banks = (num_chr > 0) ? num_chr : 1;
    size_t chr_len = chr_banks * 0x2000;
    cart->chr_rom = calloc(1, chr_len);
    cart->chr_rom_len = chr_len;
    cart->chr_ram_allocated = chr_ram;
    if (!chr_ram) {
        for (uint8_t i = 0; i < chr_banks; i++)
            memset(cart->chr_rom + i * 0x2000, 0x10 + i, 0x2000);
    }

    return cart;
}

static struct mapper *make_mapper(uint8_t num_prg, uint8_t num_chr, int chr_ram) {
    struct nes_cartridge *cart = make_cart(num_prg, num_chr, chr_ram);
    /* Use a static mapper so we can pass it by pointer safely. */
    static struct mapper map;
    memset(&map, 0, sizeof map);
    map.cartridge   = cart;
    map.mapper_id   = 1;
    map.num_prg_rom = num_prg;
    map.num_chr_rom = num_chr;
    map.cpu_read    = mapper_001_cpu_read;
    map.cpu_write   = mapper_001_cpu_write;
    map.ppu_read    = mapper_001_ppu_read;
    map.ppu_write   = mapper_001_ppu_write;
    mapper_001_init(&map);
    return &map;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/*
 * test_power_up_state
 *
 * At power-up control = 0x0C → PRG mode 3 (fix last), CHR mode 0 (8KB),
 * horizontal mirroring (bits 1:0 = 00 → MIRROR_SINGLE_LO, but 0x0C bits 1:0
 * = 00 so single-lo).
 *
 * With a 4-bank (64KB) PRG-ROM:
 *   mode 3: $8000-$BFFF = switchable (bank 0 on reset), $C000-$FFFF = last bank (3).
 */
static void test_power_up_state(void) {
    printf("\n[test_power_up_state]\n");

    struct mapper *map = make_mapper(4, 1, 0);

    /* $C000 must read from last PRG bank (bank 3, filled with 0x04). */
    uint8_t hi = mapper_001_cpu_read(map, 0xC000);
    ASSERT(hi == 4, "power-up: $C000 reads from last PRG bank");

    /* $8000 must read from bank 0 (filled with 0x01). */
    uint8_t lo = mapper_001_cpu_read(map, 0x8000);
    ASSERT(lo == 1, "power-up: $8000 reads from PRG bank 0");
}

/*
 * test_prg_mode3_bank_switch
 *
 * PRG mode 3 (default): fix last bank at $C000, switch lower bank via $E000.
 * Select bank 2; $8000 should now read from bank 2.
 */
static void test_prg_mode3_bank_switch(void) {
    printf("\n[test_prg_mode3_bank_switch]\n");

    struct mapper *map = make_mapper(4, 1, 0);

    /* Write bank 2 to the PRG bank register ($E000-$FFFF). */
    serial_write(map, 0xE000, 2);

    uint8_t lo = mapper_001_cpu_read(map, 0x8000);
    ASSERT(lo == 3, "PRG mode 3: $8000 selects bank 2 (filled with 3)");

    uint8_t hi = mapper_001_cpu_read(map, 0xC000);
    ASSERT(hi == 4, "PRG mode 3: $C000 stays fixed at last bank");
}

/*
 * test_prg_mode2_fix_first
 *
 * PRG mode 2: fix first bank at $8000, switch upper bank via $E000.
 * Set control bits [3:2] = 10 → mode 2.
 */
static void test_prg_mode2_fix_first(void) {
    printf("\n[test_prg_mode2_fix_first]\n");

    struct mapper *map = make_mapper(4, 1, 0);

    /* Write control = 0x08 → mode 2 (bits [3:2]=10), chr_mode=0, mirror=0 */
    serial_write(map, 0x8000, 0x08);

    /* Switch upper bank ($C000) to bank 3. */
    serial_write(map, 0xE000, 3);

    uint8_t lo = mapper_001_cpu_read(map, 0x8000);
    ASSERT(lo == 1, "PRG mode 2: $8000 fixed at bank 0");

    uint8_t hi = mapper_001_cpu_read(map, 0xC000);
    ASSERT(hi == 4, "PRG mode 2: $C000 switchable, bank 3 selected");
}

/*
 * test_prg_mode01_32kb
 *
 * PRG mode 0 or 1: 32KB window. Control bits [3:2]=00 → mode 0.
 * With bank register = 2 (→ 32KB bank pair 1, i.e. banks 2-3).
 * $8000-$BFFF = bank 2, $C000-$FFFF = bank 3.
 */
static void test_prg_mode01_32kb(void) {
    printf("\n[test_prg_mode01_32kb]\n");

    struct mapper *map = make_mapper(4, 1, 0);

    /* Set control to mode 0 (bits [3:2]=00): write 0x00 to $8000 */
    serial_write(map, 0x8000, 0x00);

    /* PRG bank register: value 2 → 32KB pair starting at bank (2>>1)*2 = bank 2. */
    serial_write(map, 0xE000, 2);

    uint8_t lo = mapper_001_cpu_read(map, 0x8000);
    ASSERT(lo == 3, "PRG mode 0: $8000 = bank 2 (value 3)");

    uint8_t hi = mapper_001_cpu_read(map, 0xC000);
    ASSERT(hi == 4, "PRG mode 0: $C000 = bank 3 (value 4)");
}

/*
 * test_chr_8kb_mode
 *
 * CHR mode 0 (default): 8KB bank. Switch CHR bank via $A000.
 * With 2 CHR banks: bank 0 filled with 0x10, bank 1 with 0x11.
 */
static void test_chr_8kb_mode(void) {
    printf("\n[test_chr_8kb_mode]\n");

    struct mapper *map = make_mapper(2, 2, 0);

    /* CHR bank 0 selected (power-up default). */
    uint8_t v0 = mapper_001_ppu_read(map, 0x0000);
    ASSERT(v0 == 0x10, "CHR 8KB mode: bank 0 reads 0x10");

    /* Select CHR 8KB bank 1: write value 2 (bit 0 ignored per MMC1 spec). */
    serial_write(map, 0xA000, 2);

    uint8_t v1 = mapper_001_ppu_read(map, 0x0000);
    ASSERT(v1 == 0x11, "CHR 8KB mode: after selecting bank 1 (write 2), reads 0x11");
}

/*
 * test_chr_4kb_mode
 *
 * CHR mode 1 (4KB): chr_bank_0 controls $0000-$0FFF, chr_bank_1 $1000-$1FFF.
 * With 2 CHR ROM banks (4 x 4KB banks): bank 0=0x10, bank 2=0x11 (8KB-units).
 */
static void test_chr_4kb_mode(void) {
    printf("\n[test_chr_4kb_mode]\n");

    struct mapper *map = make_mapper(2, 2, 0);

    /* Enable CHR 4KB mode: control bit 4 = 1. Keep PRG mode 3 (bits [3:2]=11).
     * Control value = 0x1C (binary 0001_1100). */
    serial_write(map, 0x8000, 0x1C);

    /* chr_bank_0 = 0 → PPU $0000 = first 4KB of CHR bank 0 = 0x10. */
    serial_write(map, 0xA000, 0);
    /* chr_bank_1 = 2 → PPU $1000 = second 4KB pair (bank 2 in 4KB terms) = 0x11. */
    serial_write(map, 0xC000, 2);

    uint8_t lo = mapper_001_ppu_read(map, 0x0000);
    ASSERT(lo == 0x10, "CHR 4KB mode: $0000 reads from 4KB bank 0");

    uint8_t hi = mapper_001_ppu_read(map, 0x1000);
    ASSERT(hi == 0x11, "CHR 4KB mode: $1000 reads from 4KB bank 2");
}

/*
 * test_chr_ram_write_8kb
 *
 * CHR-RAM in 8KB mode: writes must land at the bank-selected offset,
 * and reads back through the same path must return the written value.
 */
static void test_chr_ram_write_8kb(void) {
    printf("\n[test_chr_ram_write_8kb]\n");

    /* 1 CHR RAM bank (chr_rom_size=0 triggers chr_ram path). */
    struct mapper *map = make_mapper(2, 0, 1);
    map->num_chr_rom = 1;  /* treat the 8KB CHR-RAM as 1 bank */

    mapper_001_ppu_write(map, 0x0010, 0xAB);
    uint8_t v = mapper_001_ppu_read(map, 0x0010);
    ASSERT(v == 0xAB, "CHR-RAM 8KB: write then read-back matches");
}

/*
 * test_chr_ram_write_4kb
 *
 * CHR-RAM in 4KB mode: writes to $1000-$1FFF must go to the bank selected
 * by chr_bank_1, not always offset 0x1000 from base.
 * Use 2 banks of 8KB CHR-RAM (16KB total); chr_bank_1 = 2 (4KB bank 2 =
 * second half of second 8KB bank).
 */
static void test_chr_ram_write_4kb(void) {
    printf("\n[test_chr_ram_write_4kb]\n");

    /* 2 × 8KB CHR-RAM → 4 × 4KB banks. */
    struct nes_cartridge *cart = make_cart(2, 2, 1);
    static struct mapper map2;
    memset(&map2, 0, sizeof map2);
    map2.cartridge   = cart;
    map2.mapper_id   = 1;
    map2.num_prg_rom = 2;
    map2.num_chr_rom = 2;
    map2.cpu_read    = mapper_001_cpu_read;
    map2.cpu_write   = mapper_001_cpu_write;
    map2.ppu_read    = mapper_001_ppu_read;
    map2.ppu_write   = mapper_001_ppu_write;
    mapper_001_init(&map2);

    /* Enable CHR 4KB mode. */
    serial_write(&map2, 0x8000, 0x1C);

    /* chr_bank_1 = 2 → $1000-$1FFF maps to 4KB bank 2 = offset 0x2000 in CHR. */
    serial_write(&map2, 0xA000, 0); /* chr_bank_0 = 0 */
    serial_write(&map2, 0xC000, 2); /* chr_bank_1 = 2 */

    /* Write sentinel to $1008 (should land at CHR offset 0x2008). */
    mapper_001_ppu_write(&map2, 0x1008, 0xCD);

    /* Read back via PPU read — must return 0xCD. */
    uint8_t v = mapper_001_ppu_read(&map2, 0x1008);
    ASSERT(v == 0xCD, "CHR-RAM 4KB: write to $1008 with chr_bank_1=2 reads back correctly");

    /* Verify the write landed at the right raw offset (0x2008, not 0x1008). */
    ASSERT(cart->chr_rom[0x2008] == 0xCD, "CHR-RAM 4KB: raw byte at offset 0x2008 is correct");
    ASSERT(cart->chr_rom[0x1008] == 0x00, "CHR-RAM 4KB: raw byte at offset 0x1008 untouched");
}

/*
 * test_prg_ram
 *
 * PRG-RAM at $6000-$7FFF: reads and writes work when enabled (prg_bank bit 4 = 0),
 * and reads return 0xFF when disabled (prg_bank bit 4 = 1).
 */
static void test_prg_ram(void) {
    printf("\n[test_prg_ram]\n");

    struct mapper *map = make_mapper(2, 1, 0);

    /* Write a value to PRG-RAM. */
    mapper_001_cpu_write(map, 0x6000, 0x42);
    uint8_t v = mapper_001_cpu_read(map, 0x6000);
    ASSERT(v == 0x42, "PRG-RAM: write then read-back matches when enabled");

    /* Disable PRG-RAM (set bit 4 of PRG bank register). */
    serial_write(map, 0xE000, 0x10);
    uint8_t dis = mapper_001_cpu_read(map, 0x6000);
    ASSERT(dis == 0xFF, "PRG-RAM: reads return 0xFF when disabled");
}

/*
 * test_shift_register_reset
 *
 * Writing a byte with bit 7 set resets the shift register immediately,
 * regardless of how many bits have already been written.
 * The reset also forces control bits [3:2] = 11 (PRG mode 3).
 */
static void test_shift_register_reset(void) {
    printf("\n[test_shift_register_reset]\n");

    struct mapper *map = make_mapper(4, 1, 0);

    /* Start writing a value (3 bits in). */
    mapper_001_cpu_write(map, 0x8000, 0x01);
    mapper_001_cpu_write(map, 0x8000, 0x01);
    mapper_001_cpu_write(map, 0x8000, 0x01);

    /* Reset by writing bit 7 = 1. */
    mapper_001_cpu_write(map, 0x8000, 0x80);

    /* Now write a clean 5-bit sequence for bank 1. */
    serial_write(map, 0xE000, 1);

    /* After reset, control was forced to mode 3; $8000 switches, $C000 fixed. */
    uint8_t lo = mapper_001_cpu_read(map, 0x8000);
    ASSERT(lo == 2, "shift reset: subsequent bank switch selects bank 1 (value 2)");
}

/*
 * test_dynamic_mirroring
 *
 * Writing to the control register updates map->mirroring.
 * Bits [1:0]: 0=SINGLE_LO, 1=SINGLE_HI, 2=VERTICAL, 3=HORIZONTAL.
 */
static void test_dynamic_mirroring(void) {
    printf("\n[test_dynamic_mirroring]\n");

    struct mapper *map = make_mapper(2, 1, 0);

    /* Control = 0x0E → bits [1:0]=10 → VERTICAL */
    serial_write(map, 0x8000, 0x0E);
    ASSERT(map->mirroring == MIRROR_VERTICAL, "mirroring: 0x0E → vertical");

    /* Control = 0x0F → bits [1:0]=11 → HORIZONTAL */
    serial_write(map, 0x8000, 0x0F);
    ASSERT(map->mirroring == MIRROR_HORIZONTAL, "mirroring: 0x0F → horizontal");

    /* Control = 0x0C → bits [1:0]=00 → SINGLE_LO */
    serial_write(map, 0x8000, 0x0C);
    ASSERT(map->mirroring == MIRROR_SINGLE_LO, "mirroring: 0x0C → single-lo");

    /* Control = 0x0D → bits [1:0]=01 → SINGLE_HI */
    serial_write(map, 0x8000, 0x0D);
    ASSERT(map->mirroring == MIRROR_SINGLE_HI, "mirroring: 0x0D → single-hi");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void) {
    printf("=== Mapper 001 (MMC1) unit tests ===\n");

    test_power_up_state();
    test_prg_mode3_bank_switch();
    test_prg_mode2_fix_first();
    test_prg_mode01_32kb();
    test_chr_8kb_mode();
    test_chr_4kb_mode();
    test_chr_ram_write_8kb();
    test_chr_ram_write_4kb();
    test_prg_ram();
    test_shift_register_reset();
    test_dynamic_mirroring();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
