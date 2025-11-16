#include "mapper_000.h"
#include <stdio.h>

uint8_t mapper_000_cpu_read(struct mapper *map, uint16_t addr) {
    // If one bank, the memory at 0x8000 is mirrored at 0xc000
    // otherwise its fully mapped at 0x8000
    uint16_t map_addr = addr & ((map->num_prg_rom > 1) ? 0x7fff : 0x3fff);
    uint8_t data = map->cartridge->prg_rom[map_addr];
    return data;
}

void mapper_000_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    // PRG-ROM is read-only in NROM (Mapper 0)
    // On real NES hardware, writes to $8000-$FFFF are ignored (ROM is read-only)
    // Some test ROMs write to ROM addresses to verify CPU instruction behavior
    // Attempting to write to mmap'd ROM causes SIGSEGV, so we ignore these writes
    (void)map;
    (void)addr;
    (void)data;
    return;
}

uint8_t mapper_000_ppu_read(struct mapper *map, uint16_t addr) {
    uint8_t data = 0;

    if (map->cartridge->chr_rom) {
        data = map->cartridge->chr_rom[addr];
    }

    return data;
}

void mapper_000_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    // Only allow writes if this is CHR-RAM (allocated memory)
    // CHR-ROM from file is read-only and writes should be ignored
    if (!map || !map->cartridge) {
        return;  // Invalid mapper
    }

    // Only write if CHR-RAM was allocated (not mmap'd ROM)
    if (!map->cartridge->chr_ram_allocated) {
        // This is CHR-ROM (read-only), ignore writes
        return;
    }

    // This is CHR-RAM, allow writes with bounds checking
    if (map->cartridge->chr_rom && addr < map->cartridge->chr_rom_len) {
        map->cartridge->chr_rom[addr] = data;
    }
    return;
}