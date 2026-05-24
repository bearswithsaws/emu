#include "mapper_002.h"
#include <stdlib.h>

/* UxROM: one switchable 16KB PRG bank at $8000, one fixed last bank at $C000.
 * CHR is 8KB RAM — no banking. */

uint8_t mapper_002_cpu_read(struct mapper *map, uint16_t addr) {
    uint8_t selected = *(uint8_t *)map->ctx;

    if (addr >= 0x8000 && addr <= 0xBFFF) {
        uint32_t offset = (uint32_t)selected * 0x4000 + (addr & 0x3FFF);
        return map->cartridge->prg_rom[offset];
    }
    if (addr >= 0xC000) {
        uint32_t offset = (uint32_t)(map->num_prg_rom - 1) * 0x4000 + (addr & 0x3FFF);
        return map->cartridge->prg_rom[offset];
    }
    return 0;
}

void mapper_002_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr >= 0x8000)
        *(uint8_t *)map->ctx = data & 0x0F;
}

uint8_t mapper_002_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom)
        return map->cartridge->chr_rom[addr];
    return 0;
}

void mapper_002_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr <= 0x1FFF && map->cartridge->chr_ram_allocated &&
        map->cartridge->chr_rom) {
        map->cartridge->chr_rom[addr] = data;
    }
}

void mapper_002_init(struct mapper *map) {
    map->ctx      = calloc(1, sizeof(uint8_t));
    map->ctx_size = sizeof(uint8_t);
}
