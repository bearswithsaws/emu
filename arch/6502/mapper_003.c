#include "mapper_003.h"

/* CNROM: PRG-ROM is fixed (no banking, same as NROM).
 * CHR-ROM is bank-switched in 8KB pages via writes to $8000-$FFFF. */
static uint8_t selected_chr_bank = 0;

uint8_t mapper_003_cpu_read(struct mapper *map, uint16_t addr) {
    /* Same as NROM: mirror 16KB ROM if only one bank */
    uint16_t map_addr = addr & ((map->num_prg_rom > 1) ? 0x7FFF : 0x3FFF);
    return map->cartridge->prg_rom[map_addr];
}

void mapper_003_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    (void)map;
    if (addr >= 0x8000) {
        selected_chr_bank = data & 0x03;
    }
}

uint8_t mapper_003_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom) {
        uint32_t offset = (uint32_t)selected_chr_bank * 0x2000 + addr;
        return map->cartridge->chr_rom[offset];
    }
    return 0;
}

void mapper_003_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    /* CNROM has CHR-ROM, not CHR-RAM — writes are ignored */
    (void)map;
    (void)addr;
    (void)data;
}
