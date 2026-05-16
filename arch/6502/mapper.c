#include "mapper.h"

#include "mapper_000.h"
#include "mapper_001.h"
#include "mapper_002.h"
#include "mapper_003.h"
#include "mapper_004.h"

static struct mapper map = {0};

struct mapper *mapper_init(struct nes_cartridge *cartridge) {
    // Not sure if we need the entire cartridge or just values from it.
    // Saving both for now.
    map.cartridge = cartridge;
    map.mapper_id = cartridge->mapper_id;
    map.num_prg_rom = cartridge->hdr->prg_rom_size;
    map.num_chr_rom = cartridge->hdr->chr_rom_size;

    // Default mirroring from ROM header (mappers that support dynamic mirroring
    // will overwrite this field at runtime via their write handlers).
    map.mirroring = cartridge->hdr->flags6.mirroring ? MIRROR_VERTICAL
                                                      : MIRROR_HORIZONTAL;

    switch (map.mapper_id) {
    case 0:
        map.cpu_read = mapper_000_cpu_read;
        map.cpu_write = mapper_000_cpu_write;
        map.ppu_read = mapper_000_ppu_read;
        map.ppu_write = mapper_000_ppu_write;
        break;
    case 1:
        map.cpu_read = mapper_001_cpu_read;
        map.cpu_write = mapper_001_cpu_write;
        map.ppu_read = mapper_001_ppu_read;
        map.ppu_write = mapper_001_ppu_write;
        mapper_001_init(&map);
        break;
    case 2:
        map.cpu_read = mapper_002_cpu_read;
        map.cpu_write = mapper_002_cpu_write;
        map.ppu_read = mapper_002_ppu_read;
        map.ppu_write = mapper_002_ppu_write;
        break;
    case 3:
        map.cpu_read = mapper_003_cpu_read;
        map.cpu_write = mapper_003_cpu_write;
        map.ppu_read = mapper_003_ppu_read;
        map.ppu_write = mapper_003_ppu_write;
        break;
    case 4:
        map.cpu_read = mapper_004_cpu_read;
        map.cpu_write = mapper_004_cpu_write;
        map.ppu_read = mapper_004_ppu_read;
        map.ppu_write = mapper_004_ppu_write;
        mapper_004_init(&map);
        break;
    }

    return &map;
}