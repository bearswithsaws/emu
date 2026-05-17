#include "mapper_003.h"
#include <stdlib.h>

/* CNROM: PRG-ROM is fixed (no banking, same as NROM).
 * CHR-ROM is bank-switched in 8KB pages via writes to $8000-$FFFF. */

uint8_t mapper_003_cpu_read(struct mapper *map, uint16_t addr) {
    uint16_t map_addr = addr & ((map->num_prg_rom > 1) ? 0x7FFF : 0x3FFF);
    return map->cartridge->prg_rom[map_addr];
}

void mapper_003_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr >= 0x8000)
        *(uint8_t *)map->ctx = data & 0x03;
}

uint8_t mapper_003_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom) {
        uint8_t bank = *(uint8_t *)map->ctx;
        uint32_t offset = (uint32_t)bank * 0x2000 + addr;
        return map->cartridge->chr_rom[offset];
    }
    return 0;
}

void mapper_003_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    (void)map; (void)addr; (void)data;
}

void mapper_003_init(struct mapper *map) {
    map->ctx = calloc(1, sizeof(uint8_t));
}
