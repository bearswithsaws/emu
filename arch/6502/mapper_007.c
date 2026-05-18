#include "mapper_007.h"
#include <stdlib.h>

/* AxROM: one switchable 32KB PRG bank at $8000-$FFFF, 8KB CHR-RAM,
 * single-screen mirroring selected by bit 4 of the bank register. */

uint8_t mapper_007_cpu_read(struct mapper *map, uint16_t addr) {
    if (addr >= 0x8000) {
        uint8_t bank = *(uint8_t *)map->ctx & 0x07;
        uint32_t offset = (uint32_t)bank * 0x8000 + (addr & 0x7FFF);
        return map->cartridge->prg_rom[offset];
    }
    return 0;
}

void mapper_007_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr >= 0x8000) {
        *(uint8_t *)map->ctx = data;
        map->mirroring = (data & 0x10) ? MIRROR_SINGLE_HI : MIRROR_SINGLE_LO;
    }
}

uint8_t mapper_007_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom)
        return map->cartridge->chr_rom[addr];
    return 0;
}

void mapper_007_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr <= 0x1FFF && map->cartridge->chr_ram_allocated &&
        map->cartridge->chr_rom) {
        map->cartridge->chr_rom[addr] = data;
    }
}

void mapper_007_init(struct mapper *map) {
    map->ctx = calloc(1, sizeof(uint8_t));
    map->mirroring = MIRROR_SINGLE_LO;
}
