// Mapper 004 — MMC3 (TxROM / TKROM / TSROM)
//
// PRG-ROM: up to 512 KB banked in four 8 KB windows ($8000/$A000/$C000/$E000).
//          R6/R7 are switchable; the other two are fixed to the last two banks.
//          PRG mode bit swaps which fixed/switchable bank occupies $8000/$C000.
// CHR-ROM: up to 2 MB banked in eight 1 KB windows ($0000-$1FFF).
//          R0/R1 each select aligned 2 KB pairs; R2-R5 each select 1 KB pages.
//          CHR inversion bit swaps which registers drive the low/high 4 KB.
// PRG-RAM: 8 KB at $6000-$7FFF (write-protect bits are accepted but ignored).
// IRQ:     Scanline counter clocked once per scanline via the PPU scanline
//          callback (mapper->scanline, called at dot 260).  Counter decrements
//          (or reloads from latch) each tick; fires IRQ when it reaches zero
//          and IRQ enable is set.
//
// NOTE: The real hardware counts PPU A12 rising edges.  Our PPU fetches sprite
// CHR data inline during pixel output rather than in the proper sprite-fetch
// window (dots 257-320), which causes A12 to toggle dozens of times per
// scanline instead of once.  We therefore use a PPU scanline callback rather
// than A12 edge detection — this is the standard approach in most emulators.

#include "mapper_004.h"
#include <stdlib.h>
#include <string.h>

struct mmc3_ctx {
    // Bank select register ($8000 even write)
    uint8_t bank_select;
    uint8_t prg_mode;
    uint8_t chr_invert;

    // Bankable registers R0-R7
    uint8_t reg[8];

    // Scanline IRQ
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;

    // 8 KB battery-backed WRAM at $6000-$7FFF
    uint8_t prg_ram[0x2000];
};

void mapper_004_init(struct mapper *map) {
    struct mmc3_ctx *ctx = calloc(1, sizeof(struct mmc3_ctx));
    if (!ctx) return;
    // mirroring is seeded from ROM header by mapper.c
    map->ctx      = ctx;
    map->ctx_size = sizeof(struct mmc3_ctx);
}

// ---------------------------------------------------------------------------
// IRQ scanline counter — called once per scanline by the PPU (dot 260)
// ---------------------------------------------------------------------------

void mapper_004_scanline(struct mapper *map) {
    struct mmc3_ctx *ctx = map->ctx;

    if (ctx->irq_counter == 0 || ctx->irq_reload) {
        ctx->irq_counter = ctx->irq_latch;
        ctx->irq_reload  = 0;
    } else {
        ctx->irq_counter--;
    }

    if (ctx->irq_counter == 0 && ctx->irq_enabled) {
        map->irq_pending = 1;
    }
}

// ---------------------------------------------------------------------------
// CHR banking helper
// ---------------------------------------------------------------------------

static uint16_t resolve_chr_bank(struct mmc3_ctx *ctx, uint16_t addr,
                                 uint16_t num_1kb) {
    uint8_t slot = (addr >> 10) & 0x07;

    if (!ctx->chr_invert) {
        switch (slot) {
        case 0: return (uint16_t)(ctx->reg[0] & 0xFE) % num_1kb;
        case 1: return (uint16_t)(ctx->reg[0] | 0x01) % num_1kb;
        case 2: return (uint16_t)(ctx->reg[1] & 0xFE) % num_1kb;
        case 3: return (uint16_t)(ctx->reg[1] | 0x01) % num_1kb;
        case 4: return (uint16_t)ctx->reg[2] % num_1kb;
        case 5: return (uint16_t)ctx->reg[3] % num_1kb;
        case 6: return (uint16_t)ctx->reg[4] % num_1kb;
        case 7: return (uint16_t)ctx->reg[5] % num_1kb;
        }
    } else {
        switch (slot) {
        case 0: return (uint16_t)ctx->reg[2] % num_1kb;
        case 1: return (uint16_t)ctx->reg[3] % num_1kb;
        case 2: return (uint16_t)ctx->reg[4] % num_1kb;
        case 3: return (uint16_t)ctx->reg[5] % num_1kb;
        case 4: return (uint16_t)(ctx->reg[0] & 0xFE) % num_1kb;
        case 5: return (uint16_t)(ctx->reg[0] | 0x01) % num_1kb;
        case 6: return (uint16_t)(ctx->reg[1] & 0xFE) % num_1kb;
        case 7: return (uint16_t)(ctx->reg[1] | 0x01) % num_1kb;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_004_cpu_read(struct mapper *map, uint16_t addr) {
    struct mmc3_ctx *ctx = map->ctx;

    if (addr >= 0x6000 && addr <= 0x7FFF)
        return ctx->prg_ram[addr & 0x1FFF];

    if (addr < 0x8000) return 0;

    uint16_t num_8kb = map->num_prg_rom * 2;
    uint16_t bank;

    if (addr <= 0x9FFF) {
        bank = ctx->prg_mode ? (num_8kb - 2) : (ctx->reg[6] % num_8kb);
    } else if (addr <= 0xBFFF) {
        bank = ctx->reg[7] % num_8kb;
    } else if (addr <= 0xDFFF) {
        bank = ctx->prg_mode ? (ctx->reg[6] % num_8kb) : (num_8kb - 2);
    } else {
        bank = num_8kb - 1;
    }

    uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
    return map->cartridge->prg_rom[offset];
}

void mapper_004_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct mmc3_ctx *ctx = map->ctx;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        ctx->prg_ram[addr & 0x1FFF] = data;
        return;
    }

    if (addr < 0x8000) return;

    uint8_t odd = addr & 0x01;

    if (addr <= 0x9FFF) {
        if (!odd) {
            ctx->bank_select = data & 0x07;
            ctx->prg_mode    = (data >> 6) & 0x01;
            ctx->chr_invert  = (data >> 7) & 0x01;
        } else {
            ctx->reg[ctx->bank_select] = data;
        }
    } else if (addr <= 0xBFFF) {
        if (!odd)
            map->mirroring = (data & 0x01) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        // $A001: PRG-RAM protect — accepted but ignored
    } else if (addr <= 0xDFFF) {
        if (!odd) {
            ctx->irq_latch = data;
        } else {
            ctx->irq_counter = 0;
            ctx->irq_reload  = 1;
        }
    } else {
        if (!odd) {
            ctx->irq_enabled = 0;
            map->irq_pending = 0;
        } else {
            ctx->irq_enabled = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// PPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_004_ppu_read(struct mapper *map, uint16_t addr) {
    struct mmc3_ctx *ctx = map->ctx;

    if (addr > 0x1FFF) return 0;
    if (!map->cartridge->chr_rom) return 0;

    if (map->num_chr_rom == 0 || map->cartridge->chr_ram_allocated)
        return map->cartridge->chr_rom[addr & 0x1FFF];

    uint16_t num_1kb = (uint16_t)map->num_chr_rom * 8;
    uint16_t bank    = resolve_chr_bank(ctx, addr, num_1kb);
    uint32_t offset  = (uint32_t)bank * 0x0400 + (addr & 0x03FF);

    if (offset < map->cartridge->chr_rom_len)
        return map->cartridge->chr_rom[offset];
    return 0;
}

void mapper_004_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (!map || !map->cartridge) return;
    if (addr > 0x1FFF) return;
    if (!map->cartridge->chr_ram_allocated) return;

    if (map->cartridge->chr_rom)
        map->cartridge->chr_rom[addr & 0x1FFF] = data;
}
