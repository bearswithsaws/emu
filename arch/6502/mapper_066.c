#include "mapper_066.h"
#include <stdlib.h>

/* GxROM: one switchable 32KB PRG bank (bits 4-5) and one switchable 8KB CHR
 * bank (bits 0-1), selected by a single write to $8000-$FFFF. */

uint8_t mapper_066_cpu_read(struct mapper *map, uint16_t addr) {
    if (addr >= 0x8000) {
        uint8_t bank = (*(uint8_t *)map->ctx >> 4) & 0x03;
        uint32_t offset = (uint32_t)bank * 0x8000 + (addr & 0x7FFF);
        return map->cartridge->prg_rom[offset];
    }
    return 0;
}

void mapper_066_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr >= 0x8000)
        *(uint8_t *)map->ctx = data;
}

uint8_t mapper_066_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom) {
        uint8_t bank = *(uint8_t *)map->ctx & 0x03;
        uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
        return map->cartridge->chr_rom[offset];
    }
    return 0;
}

void mapper_066_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    (void)map; (void)addr; (void)data;
    /* GxROM uses CHR-ROM only — writes are no-ops. */
}

void mapper_066_init(struct mapper *map) {
    map->ctx = calloc(1, sizeof(uint8_t));
}
