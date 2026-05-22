#include "mapper_001.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mmc1_ctx {
    // 5-bit serial shift register
    uint8_t shift_register;
    uint8_t write_count;

    // Internal registers
    uint8_t control;
    uint8_t chr_bank_0;
    uint8_t chr_bank_1;
    uint8_t prg_bank;

    // Decoded fields from control
    uint8_t mirroring;
    uint8_t prg_mode;
    uint8_t chr_mode;

    // 8 KB battery-backed PRG-RAM at $6000-$7FFF
    uint8_t prg_ram[0x2000];
};

static uint8_t mmc1_to_mirror(uint8_t m) {
    switch (m) {
    case 0:  return MIRROR_SINGLE_LO;
    case 1:  return MIRROR_SINGLE_HI;
    case 2:  return MIRROR_VERTICAL;
    default: return MIRROR_HORIZONTAL;
    }
}

static void mmc1_update_control(struct mapper *map, struct mmc1_ctx *ctx) {
    ctx->mirroring = ctx->control & 0x03;
    ctx->prg_mode  = (ctx->control >> 2) & 0x03;
    ctx->chr_mode  = (ctx->control >> 4) & 0x01;
    map->mirroring = mmc1_to_mirror(ctx->mirroring);
}

void mapper_001_init(struct mapper *map) {
    struct mmc1_ctx *ctx = calloc(1, sizeof(struct mmc1_ctx));
    if (!ctx) return;

    // Power-up state: control = $0C (fix last PRG bank, 8 KB CHR mode)
    ctx->control = 0x0C;
    mmc1_update_control(map, ctx);
    map->ctx          = ctx;
    map->ctx_size     = sizeof(struct mmc1_ctx);
    map->prg_ram      = ctx->prg_ram;
    map->prg_ram_size = sizeof(ctx->prg_ram);
}

static void mmc1_write_register(struct mapper *map, struct mmc1_ctx *ctx,
                                uint16_t addr, uint8_t data) {
    if (data & 0x80) {
        ctx->shift_register = 0;
        ctx->write_count    = 0;
        ctx->control |= 0x0C;
        mmc1_update_control(map, ctx);
        return;
    }

    ctx->shift_register = (ctx->shift_register >> 1) | ((data & 0x01) << 4);
    ctx->write_count++;

    if (ctx->write_count == 5) {
        uint8_t value = ctx->shift_register;

        if (addr <= 0x9FFF) {
            ctx->control = value;
            mmc1_update_control(map, ctx);
        } else if (addr <= 0xBFFF) {
            ctx->chr_bank_0 = value;
        } else if (addr <= 0xDFFF) {
            ctx->chr_bank_1 = value;
        } else {
            ctx->prg_bank = value;
        }

        ctx->shift_register = 0;
        ctx->write_count    = 0;
    }
}

uint8_t mapper_001_cpu_read(struct mapper *map, uint16_t addr) {
    struct mmc1_ctx *ctx = map->ctx;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (ctx->prg_bank & 0x10) return 0xFF;
        return ctx->prg_ram[addr & 0x1FFF];
    }

    uint32_t offset = 0;
    uint8_t  n      = map->num_prg_rom;

    if (ctx->prg_mode <= 1) {
        /* 32KB window: bit 0 of prg_bank is ignored; select a pair of 16KB banks. */
        uint8_t bank32   = (ctx->prg_bank >> 1) & 0x0F;
        uint8_t num_32k  = (n / 2) ? (n / 2) : 1;
        offset = (((bank32 % num_32k) * 2) * 0x4000) + (addr & 0x7FFF);
    } else if (ctx->prg_mode == 2) {
        if (addr <= 0xBFFF) {
            offset = addr & 0x3FFF;
        } else {
            uint8_t bank = ctx->prg_bank & 0x0F;
            offset = ((bank % n) * 0x4000) + (addr & 0x3FFF);
        }
    } else {
        if (addr <= 0xBFFF) {
            uint8_t bank = ctx->prg_bank & 0x0F;
            offset = ((bank % n) * 0x4000) + (addr & 0x3FFF);
        } else {
            offset = ((n - 1) * 0x4000) + (addr & 0x3FFF);
        }
    }

    return map->cartridge->prg_rom[offset];
}

void mapper_001_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct mmc1_ctx *ctx = map->ctx;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (!(ctx->prg_bank & 0x10))
            ctx->prg_ram[addr & 0x1FFF] = data;
        return;
    }

    if (addr >= 0x8000)
        mmc1_write_register(map, ctx, addr, data);
}

uint8_t mapper_001_ppu_read(struct mapper *map, uint16_t addr) {
    struct mmc1_ctx *ctx = map->ctx;

    if (!map->cartridge->chr_rom) return 0;

    uint32_t offset = 0;
    uint8_t n = map->num_chr_rom ? map->num_chr_rom : 1;

    if (ctx->chr_mode == 0) {
        /* 8KB window: bit 0 of chr_bank_0 is ignored. */
        uint8_t bank = (ctx->chr_bank_0 >> 1) & 0x1F;
        offset = ((bank % n) * 0x2000) + (addr & 0x1FFF);
    } else {
        /* 4KB window: two independently switchable 4KB banks. */
        if (addr <= 0x0FFF) {
            uint8_t bank = ctx->chr_bank_0 & 0x1F;
            offset = ((bank % (n * 2)) * 0x1000) + (addr & 0x0FFF);
        } else {
            uint8_t bank = ctx->chr_bank_1 & 0x1F;
            offset = ((bank % (n * 2)) * 0x1000) + (addr & 0x0FFF);
        }
    }

    if (offset < map->cartridge->chr_rom_len)
        return map->cartridge->chr_rom[offset];

    return 0;
}

void mapper_001_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (!map || !map->cartridge) return;
    if (!map->cartridge->chr_ram_allocated) return;

    struct mmc1_ctx *ctx = map->ctx;
    uint8_t n = map->num_chr_rom ? map->num_chr_rom : 1;
    uint32_t offset;

    if (ctx->chr_mode == 0) {
        uint8_t bank = (ctx->chr_bank_0 >> 1) & 0x1F;
        offset = ((bank % n) * 0x2000) + (addr & 0x1FFF);
    } else {
        if (addr <= 0x0FFF) {
            uint8_t bank = ctx->chr_bank_0 & 0x1F;
            offset = ((bank % (n * 2)) * 0x1000) + (addr & 0x0FFF);
        } else {
            uint8_t bank = ctx->chr_bank_1 & 0x1F;
            offset = ((bank % (n * 2)) * 0x1000) + (addr & 0x0FFF);
        }
    }

    if (map->cartridge->chr_rom && offset < map->cartridge->chr_rom_len)
        map->cartridge->chr_rom[offset] = data;
}
