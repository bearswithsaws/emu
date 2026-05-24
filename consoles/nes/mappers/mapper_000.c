#include "mapper_000.h"

#include <stdlib.h>
#include <string.h>

struct nrom_ctx {
    uint8_t prg_ram[0x2000]; /* 8 KB work RAM at $6000-$7FFF */
};

void mapper_000_init(struct mapper *map) {
    struct nrom_ctx *ctx = calloc(1, sizeof(struct nrom_ctx));
    if (!ctx) return;
    map->ctx      = ctx;
    map->ctx_size = sizeof(struct nrom_ctx);
}

uint8_t mapper_000_cpu_read(struct mapper *map, uint16_t addr) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        struct nrom_ctx *ctx = map->ctx;
        return ctx ? ctx->prg_ram[addr & 0x1FFF] : 0xFF;
    }
    /* $8000-$FFFF: 16 KB mirrored or 32 KB straight */
    uint16_t map_addr = addr & ((map->num_prg_rom > 1) ? 0x7FFF : 0x3FFF);
    return map->cartridge->prg_rom[map_addr];
}

void mapper_000_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        struct nrom_ctx *ctx = map->ctx;
        if (ctx) ctx->prg_ram[addr & 0x1FFF] = data;
    }
    /* writes to $8000-$FFFF: ROM is read-only, ignore */
}

uint8_t mapper_000_ppu_read(struct mapper *map, uint16_t addr) {
    if (map->cartridge->chr_rom)
        return map->cartridge->chr_rom[addr];
    return 0;
}

void mapper_000_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (!map || !map->cartridge) return;
    if (!map->cartridge->chr_ram_allocated) return;
    if (map->cartridge->chr_rom && addr < map->cartridge->chr_rom_len)
        map->cartridge->chr_rom[addr] = data;
}
