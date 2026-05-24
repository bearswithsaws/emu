#include "mapper_011.h"
#include <stdlib.h>

/* Color Dreams: one switchable 32KB PRG bank (bits 0-1) and one switchable
 * 8KB CHR bank (bits 4-7), selected by a single write to $8000-$FFFF. */

uint8_t mapper_011_cpu_read(struct mapper *map, uint16_t addr) {
    if (addr >= 0x8000) {
        uint8_t bank = *(uint8_t *)map->ctx & 0x03;
        uint32_t offset = (uint32_t)bank * 0x8000 + (addr & 0x7FFF);
        return map->cartridge->prg_rom[offset];
    }
    return 0;
}

void mapper_011_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr >= 0x8000)
        *(uint8_t *)map->ctx = data;
}

uint8_t mapper_011_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom) {
        uint8_t bank = (*(uint8_t *)map->ctx >> 4) & 0x0F;
        uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
        return map->cartridge->chr_rom[offset];
    }
    return 0;
}

void mapper_011_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr <= 0x1FFF && map->cartridge->chr_ram_allocated &&
        map->cartridge->chr_rom) {
        uint8_t bank = (*(uint8_t *)map->ctx >> 4) & 0x0F;
        uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
        map->cartridge->chr_rom[offset] = data;
    }
}

void mapper_011_init(struct mapper *map) {
    map->ctx      = calloc(1, sizeof(uint8_t));
    map->ctx_size = sizeof(uint8_t);
}
