#include "mapper.h"

#include "mapper_000.h"
#include "mapper_001.h"
#include "mapper_002.h"
#include "mapper_003.h"
#include "mapper_004.h"
#include "mapper_007.h"
#include "mapper_009.h"
#include "mapper_011.h"
#include "mapper_066.h"

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
        mapper_002_init(&map);
        break;
    case 3:
        map.cpu_read = mapper_003_cpu_read;
        map.cpu_write = mapper_003_cpu_write;
        map.ppu_read = mapper_003_ppu_read;
        map.ppu_write = mapper_003_ppu_write;
        mapper_003_init(&map);
        break;
    case 4:
        map.cpu_read  = mapper_004_cpu_read;
        map.cpu_write = mapper_004_cpu_write;
        map.ppu_read  = mapper_004_ppu_read;
        map.ppu_write = mapper_004_ppu_write;
        map.scanline  = mapper_004_scanline;
        mapper_004_init(&map);
        break;
    case 7:
        map.cpu_read  = mapper_007_cpu_read;
        map.cpu_write = mapper_007_cpu_write;
        map.ppu_read  = mapper_007_ppu_read;
        map.ppu_write = mapper_007_ppu_write;
        mapper_007_init(&map);
        break;
    case 9:
        map.cpu_read  = mapper_009_cpu_read;
        map.cpu_write = mapper_009_cpu_write;
        map.ppu_read  = mapper_009_ppu_read;
        map.ppu_write = mapper_009_ppu_write;
        mapper_009_init(&map);
        break;
    case 11:
        map.cpu_read  = mapper_011_cpu_read;
        map.cpu_write = mapper_011_cpu_write;
        map.ppu_read  = mapper_011_ppu_read;
        map.ppu_write = mapper_011_ppu_write;
        mapper_011_init(&map);
        break;
    case 66:
        map.cpu_read  = mapper_066_cpu_read;
        map.cpu_write = mapper_066_cpu_write;
        map.ppu_read  = mapper_066_ppu_read;
        map.ppu_write = mapper_066_ppu_write;
        mapper_066_init(&map);
        break;
    }

    return &map;
}