#ifndef __MAPPER_005_H__
#define __MAPPER_005_H__

#include "mapper.h"

// Mapper 005 — MMC5 (ExROM)
//
// One of the most capable NES mappers, used by ~25 licensed titles including
// Castlevania III: Dracula's Curse and Just Breed.
//
// This initialiser wires all function pointers on the struct mapper that
// mapper_init() allocates.

void mapper_005_init(struct mapper *map);

uint8_t mapper_005_cpu_read(struct mapper *map, uint16_t addr);
void    mapper_005_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_005_ppu_read(struct mapper *map, uint16_t addr);
void    mapper_005_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

// Nametable hooks — called by the PPU instead of the default CIRAM path.
uint8_t mapper_005_nt_read(struct mapper *map, uint16_t addr);
void    mapper_005_nt_write(struct mapper *map, uint16_t addr, uint8_t data);

// PPU scanline hook — used for the in-frame scanline IRQ counter.
void mapper_005_scanline(struct mapper *map);

#endif /* __MAPPER_005_H__ */
