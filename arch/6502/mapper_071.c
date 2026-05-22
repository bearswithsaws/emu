#include "mapper_071.h"
#include <stdlib.h>

/* Camerica/Codemasters (Mapper 071):
 *   - 16KB switchable PRG bank at $8000-$BFFF; last 16KB of PRG-ROM fixed at
 *     $C000-$FFFF.
 *   - Bank selected by writes to $8000-$FFFF (bits 3-0).
 *   - CHR is 8KB RAM — no banking.
 *   - Fire Hawk variant: writes to $9000-$9FFF select single-screen mirroring.
 *     Bit 4: 0 = MIRROR_SINGLE_LO, 1 = MIRROR_SINGLE_HI.
 *     Normal Camerica games only write to $C000-$FFFF and never touch $9000.
 */

struct mapper_071_ctx {
    uint8_t prg_bank; /* currently selected 16KB PRG bank at $8000 */
};

uint8_t mapper_071_cpu_read(struct mapper *map, uint16_t addr) {
    struct mapper_071_ctx *ctx = (struct mapper_071_ctx *)map->ctx;

    if (addr >= 0x8000 && addr <= 0xBFFF) {
        uint32_t offset = (uint32_t)ctx->prg_bank * 0x4000 + (addr & 0x3FFF);
        return map->cartridge->prg_rom[offset];
    }
    if (addr >= 0xC000) {
        uint32_t offset =
            (uint32_t)(map->num_prg_rom - 1) * 0x4000 + (addr & 0x3FFF);
        return map->cartridge->prg_rom[offset];
    }
    return 0;
}

void mapper_071_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct mapper_071_ctx *ctx = (struct mapper_071_ctx *)map->ctx;

    /* Fire Hawk mirroring register: $9000-$9FFF */
    if (addr >= 0x9000 && addr <= 0x9FFF) {
        map->mirroring =
            (data & 0x10) ? MIRROR_SINGLE_HI : MIRROR_SINGLE_LO;
        return;
    }

    /* PRG bank select: any write to $8000-$FFFF */
    if (addr >= 0x8000) {
        ctx->prg_bank = data & 0x0F;
    }
}

uint8_t mapper_071_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom)
        return map->cartridge->chr_rom[addr];
    return 0;
}

void mapper_071_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr <= 0x1FFF && map->cartridge->chr_ram_allocated &&
        map->cartridge->chr_rom) {
        map->cartridge->chr_rom[addr] = data;
    }
}

void mapper_071_init(struct mapper *map) {
    map->ctx      = calloc(1, sizeof(struct mapper_071_ctx));
    map->ctx_size = sizeof(struct mapper_071_ctx);
}
