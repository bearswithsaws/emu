#include "mapper_002.h"

/* UxROM: one switchable 16KB PRG bank at $8000, one fixed last bank at $C000.
 * CHR is 8KB RAM — no banking. */
static uint8_t selected_prg_bank = 0;

uint8_t mapper_002_cpu_read(struct mapper *map, uint16_t addr) {
    if (addr >= 0x8000 && addr <= 0xBFFF) {
        uint32_t offset = (uint32_t)selected_prg_bank * 0x4000 + (addr & 0x3FFF);
        return map->cartridge->prg_rom[offset];
    }
    if (addr >= 0xC000) {
        uint32_t last_bank = map->num_prg_rom - 1;
        uint32_t offset = last_bank * 0x4000 + (addr & 0x3FFF);
        return map->cartridge->prg_rom[offset];
    }
    return 0;
}

void mapper_002_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    (void)map;
    if (addr >= 0x8000) {
        selected_prg_bank = data & 0x0F;
    }
}

uint8_t mapper_002_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr <= 0x1FFF && map->cartridge->chr_rom) {
        return map->cartridge->chr_rom[addr];
    }
    return 0;
}

void mapper_002_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    /* CHR-RAM write (games with chr_rom_size=0 get allocated CHR-RAM) */
    if (addr <= 0x1FFF && map->cartridge->chr_ram_allocated &&
        map->cartridge->chr_rom) {
        map->cartridge->chr_rom[addr] = data;
    }
}
