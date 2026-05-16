// Mapper 004 — MMC3 (TxROM / TKROM / TSROM)
//
// PRG-ROM: up to 512 KB banked in four 8 KB windows ($8000/$A000/$C000/$E000).
//          R6/R7 are switchable; the other two are fixed to the last two banks.
//          PRG mode bit swaps which fixed/switchable bank occupies $8000/$C000.
// CHR-ROM: up to 256 KB banked in eight 1 KB windows ($0000-$1FFF).
//          R0/R1 each select aligned 2 KB pairs; R2-R5 each select 1 KB pages.
//          CHR inversion bit swaps which registers drive the low/high 4 KB.
// PRG-RAM: 8 KB at $6000-$7FFF (write-protect bits are accepted but ignored).
// IRQ:     Scanline counter driven by rising edges of PPU A12.  Counter
//          decrements (or reloads from latch) on each edge; fires IRQ when it
//          reaches zero and IRQ enable is set.

#include "mapper_004.h"
#include <string.h>

struct mmc3_state {
    // Bank select register ($8000 even write)
    uint8_t bank_select;  // which of R0-R7 the next $8001 write targets
    uint8_t prg_mode;     // 0: R6 at $8000, fixed at $C000; 1: swapped
    uint8_t chr_invert;   // 0: R0/R1 low, R2-R5 high; 1: swapped

    // Bankable registers R0-R7
    uint8_t reg[8];

    // Scanline IRQ
    uint8_t irq_latch;    // value reloaded into counter
    uint8_t irq_counter;  // decrements on A12 rising edge
    uint8_t irq_reload;   // pending reload flag (set by $C001 write)
    uint8_t irq_enabled;  // IRQ fires only when this is set

    // A12 edge detection
    uint8_t last_a12;
};

static struct mmc3_state mmc3 = {0};
static uint8_t prg_ram[0x2000] = {0};  // 8 KB battery-backed WRAM

void mapper_004_init(struct mapper *map) {
    (void)map;  // mirroring is seeded from ROM header by mapper.c
    memset(&mmc3, 0, sizeof(mmc3));
    memset(prg_ram, 0, sizeof(prg_ram));
}

// ---------------------------------------------------------------------------
// IRQ scanline counter
// ---------------------------------------------------------------------------

// Called on every PPU address presentation (ppu_read / ppu_write).
// The MMC3 watches PPU A12: rising edge clocks the scanline counter.
static void clock_irq(struct mapper *map, uint16_t addr) {
    uint8_t a12 = (addr >> 12) & 0x01;

    if (!mmc3.last_a12 && a12) {
        // Rising edge detected
        if (mmc3.irq_counter == 0 || mmc3.irq_reload) {
            mmc3.irq_counter = mmc3.irq_latch;
            mmc3.irq_reload  = 0;
        } else {
            mmc3.irq_counter--;
        }

        if (mmc3.irq_counter == 0 && mmc3.irq_enabled) {
            map->irq_pending = 1;
        }
    }

    mmc3.last_a12 = a12;
}

// ---------------------------------------------------------------------------
// CHR banking helper
// ---------------------------------------------------------------------------

// Returns the 1 KB CHR bank index for the given PPU address (0-$1FFF).
// Handles the chr_invert mode and the 2 KB alignment of R0/R1.
static uint8_t resolve_chr_bank(uint16_t addr, uint8_t num_1kb) {
    uint8_t slot = (addr >> 10) & 0x07;  // 0-7: which 1 KB window

    if (!mmc3.chr_invert) {
        // Standard: R0/R1 select 2 KB pairs at $0000-$0FFF; R2-R5 at $1000-$1FFF
        switch (slot) {
        case 0: return (mmc3.reg[0] & 0xFE) % num_1kb;
        case 1: return (mmc3.reg[0] | 0x01) % num_1kb;
        case 2: return (mmc3.reg[1] & 0xFE) % num_1kb;
        case 3: return (mmc3.reg[1] | 0x01) % num_1kb;
        case 4: return mmc3.reg[2] % num_1kb;
        case 5: return mmc3.reg[3] % num_1kb;
        case 6: return mmc3.reg[4] % num_1kb;
        case 7: return mmc3.reg[5] % num_1kb;
        }
    } else {
        // Inverted: R2-R5 at $0000-$0FFF; R0/R1 select 2 KB pairs at $1000-$1FFF
        switch (slot) {
        case 0: return mmc3.reg[2] % num_1kb;
        case 1: return mmc3.reg[3] % num_1kb;
        case 2: return mmc3.reg[4] % num_1kb;
        case 3: return mmc3.reg[5] % num_1kb;
        case 4: return (mmc3.reg[0] & 0xFE) % num_1kb;
        case 5: return (mmc3.reg[0] | 0x01) % num_1kb;
        case 6: return (mmc3.reg[1] & 0xFE) % num_1kb;
        case 7: return (mmc3.reg[1] | 0x01) % num_1kb;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_004_cpu_read(struct mapper *map, uint16_t addr) {
    // PRG-RAM at $6000-$7FFF
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        return prg_ram[addr & 0x1FFF];
    }

    if (addr < 0x8000) return 0;

    // Four 8 KB PRG windows: $8000 / $A000 / $C000 / $E000
    // Number of 8 KB banks in ROM (each iNES PRG bank = 16 KB = two 8 KB banks)
    uint8_t  num_8kb = map->num_prg_rom * 2;
    uint8_t  bank;
    uint32_t offset;

    if (addr <= 0x9FFF) {
        // $8000: R6 in mode 0; fixed second-to-last in mode 1
        bank = mmc3.prg_mode ? (num_8kb - 2)
                             : (mmc3.reg[6] % num_8kb);
    } else if (addr <= 0xBFFF) {
        // $A000: always R7
        bank = mmc3.reg[7] % num_8kb;
    } else if (addr <= 0xDFFF) {
        // $C000: fixed second-to-last in mode 0; R6 in mode 1
        bank = mmc3.prg_mode ? (mmc3.reg[6] % num_8kb)
                             : (num_8kb - 2);
    } else {
        // $E000: always last bank
        bank = num_8kb - 1;
    }

    offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
    return map->cartridge->prg_rom[offset];
}

void mapper_004_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    // PRG-RAM at $6000-$7FFF
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        prg_ram[addr & 0x1FFF] = data;
        return;
    }

    if (addr < 0x8000) return;

    uint8_t odd = addr & 0x01;

    if (addr <= 0x9FFF) {
        if (!odd) {
            // $8000 (even) — Bank Select
            mmc3.bank_select = data & 0x07;
            mmc3.prg_mode    = (data >> 6) & 0x01;
            mmc3.chr_invert  = (data >> 7) & 0x01;
        } else {
            // $8001 (odd) — Bank Data: write to selected R0-R7
            mmc3.reg[mmc3.bank_select] = data;
        }
    } else if (addr <= 0xBFFF) {
        if (!odd) {
            // $A000 (even) — Mirroring (ignored on 4-screen carts)
            map->mirroring = (data & 0x01) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        }
        // $A001 (odd) — PRG-RAM Protect (accept but ignore for compatibility)
    } else if (addr <= 0xDFFF) {
        if (!odd) {
            // $C000 (even) — IRQ Latch
            mmc3.irq_latch = data;
        } else {
            // $C001 (odd) — IRQ Reload: zero counter + request reload on next A12
            mmc3.irq_counter = 0;
            mmc3.irq_reload  = 1;
        }
    } else {
        if (!odd) {
            // $E000 (even) — IRQ Disable + Acknowledge
            mmc3.irq_enabled = 0;
            map->irq_pending = 0;
        } else {
            // $E001 (odd) — IRQ Enable
            mmc3.irq_enabled = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// PPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_004_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr > 0x1FFF) return 0;

    clock_irq(map, addr);

    if (!map->cartridge->chr_rom) return 0;

    // CHR-RAM: single 8 KB bank, no banking
    if (map->num_chr_rom == 0 || map->cartridge->chr_ram_allocated) {
        return map->cartridge->chr_rom[addr & 0x1FFF];
    }

    uint8_t  num_1kb = map->num_chr_rom * 8;  // iNES CHR bank = 8 KB = 8 × 1 KB
    uint8_t  bank    = resolve_chr_bank(addr, num_1kb);
    uint32_t offset  = (uint32_t)bank * 0x0400 + (addr & 0x03FF);

    if (offset < map->cartridge->chr_rom_len) {
        return map->cartridge->chr_rom[offset];
    }
    return 0;
}

void mapper_004_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (!map || !map->cartridge) return;
    if (addr > 0x1FFF) return;

    clock_irq(map, addr);

    // Only CHR-RAM carts accept PPU writes
    if (!map->cartridge->chr_ram_allocated) return;

    if (map->cartridge->chr_rom) {
        map->cartridge->chr_rom[addr & 0x1FFF] = data;
    }
}
