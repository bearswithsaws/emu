// Mapper 009 — PxROM (MMC2)
//
// Used exclusively by Punch-Out!! and Mike Tyson's Punch-Out!!
//
// PRG-ROM: 128 KB banked in four 8 KB windows.
//   $8000-$9FFF: switchable via $A000-$AFFF register (bits 3:0)
//   $A000-$BFFF: fixed to bank N-3
//   $C000-$DFFF: fixed to bank N-2
//   $E000-$FFFF: fixed to bank N-1 (last)
//
// CHR-ROM: 128 KB.  Two 4 KB windows each driven by a latch:
//   $0000-$0FFF: chr_bank[0] when latch0=FD, chr_bank[1] when latch0=FE
//   $1000-$1FFF: chr_bank[2] when latch1=FD, chr_bank[3] when latch1=FE
//
// Latch update rules (triggers fire AFTER the read returns):
//   PPU reads $0FD0-$0FDF → latch0 = FD
//   PPU reads $0FE0-$0FEF → latch0 = FE
//   PPU reads $1FD0-$1FDF → latch1 = FD
//   PPU reads $1FE0-$1FEF → latch1 = FE
//
// Registers (CPU writes to $A000-$FFFF):
//   $A000-$AFFF: PRG bank select (bits 3:0)
//   $B000-$BFFF: CHR bank 0 / latch0=FD (bits 4:0)
//   $C000-$CFFF: CHR bank 0 / latch0=FE (bits 4:0)
//   $D000-$DFFF: CHR bank 1 / latch1=FD (bits 4:0)
//   $E000-$EFFF: CHR bank 1 / latch1=FE (bits 4:0)
//   $F000-$FFFF: mirroring (bit 0: 0=vertical, 1=horizontal)

#include "mapper_009.h"
#include <stdlib.h>

struct mmc2_ctx {
    uint8_t prg_bank;    /* switchable 8 KB bank at $8000-$9FFF */
    uint8_t chr_bank[4]; /* [0]=lo/FD [1]=lo/FE [2]=hi/FD [3]=hi/FE */
    uint8_t latch[2];    /* latch[0] and latch[1], values 0xFD or 0xFE */
};

void mapper_009_init(struct mapper *map) {
    struct mmc2_ctx *ctx = calloc(1, sizeof(struct mmc2_ctx));
    if (!ctx) return;
    ctx->latch[0] = 0xFE;
    ctx->latch[1] = 0xFE;
    map->ctx      = ctx;
    map->ctx_size = sizeof(struct mmc2_ctx);
}

uint8_t mapper_009_cpu_read(struct mapper *map, uint16_t addr) {
    struct mmc2_ctx *ctx = map->ctx;
    if (addr < 0x8000) return 0;

    uint16_t num_8kb = (uint16_t)map->num_prg_rom * 2;
    uint16_t bank;

    if (addr <= 0x9FFF)      bank = ctx->prg_bank % num_8kb;
    else if (addr <= 0xBFFF) bank = num_8kb - 3;
    else if (addr <= 0xDFFF) bank = num_8kb - 2;
    else                     bank = num_8kb - 1;

    uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
    return map->cartridge->prg_rom[offset];
}

void mapper_009_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct mmc2_ctx *ctx = map->ctx;
    if (addr < 0xA000) return;

    if      (addr <= 0xAFFF) ctx->prg_bank    = data & 0x0F;
    else if (addr <= 0xBFFF) ctx->chr_bank[0] = data & 0x1F;
    else if (addr <= 0xCFFF) ctx->chr_bank[1] = data & 0x1F;
    else if (addr <= 0xDFFF) ctx->chr_bank[2] = data & 0x1F;
    else if (addr <= 0xEFFF) ctx->chr_bank[3] = data & 0x1F;
    else map->mirroring = (data & 0x01) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
}

uint8_t mapper_009_ppu_read(struct mapper *map, uint16_t addr) {
    struct mmc2_ctx *ctx = map->ctx;
    if (addr > 0x1FFF) return 0;
    if (!map->cartridge->chr_rom) return 0;

    uint8_t half     = (addr >= 0x1000) ? 1 : 0;
    uint8_t bank_idx = (uint8_t)(half * 2) + (ctx->latch[half] == 0xFE ? 1 : 0);

    uint16_t num_4kb = (uint16_t)map->num_chr_rom * 2;
    uint16_t bank    = ctx->chr_bank[bank_idx] % num_4kb;
    uint32_t offset  = (uint32_t)bank * 0x1000 + (addr & 0x0FFF);

    uint8_t data = 0;
    if (offset < map->cartridge->chr_rom_len)
        data = map->cartridge->chr_rom[offset];

    /* Latch update fires after the read */
    if      (addr >= 0x0FD0 && addr <= 0x0FDF) ctx->latch[0] = 0xFD;
    else if (addr >= 0x0FE0 && addr <= 0x0FEF) ctx->latch[0] = 0xFE;
    else if (addr >= 0x1FD0 && addr <= 0x1FDF) ctx->latch[1] = 0xFD;
    else if (addr >= 0x1FE0 && addr <= 0x1FEF) ctx->latch[1] = 0xFE;

    return data;
}

void mapper_009_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    /* CHR-ROM only on MMC2 — writes are ignored */
    (void)map; (void)addr; (void)data;
}
