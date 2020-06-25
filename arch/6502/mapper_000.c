#include "mapper_000.h"
#include <stdio.h>

uint8_t mapper_000_cpu_read( struct mapper *map, uint16_t addr )
{
	// If one bank, the memory at 0x8000 is mirrored at 0xc000
	// otherwise its fully mapped at 0x8000
	uint16_t map_addr = addr & ( ( map->num_prg_rom > 1 ) ? 0x7fff : 0x3fff );
	uint8_t data = map->cartridge->prg_rom[ map_addr ];
	return data;
}

void mapper_000_cpu_write( struct mapper *map, uint16_t addr, uint8_t data )
{
	uint16_t map_addr = addr & ( ( map->num_prg_rom > 1 ) ? 0x7fff : 0x3fff );
	map->cartridge->prg_rom[ map_addr ] = data;

	return;
}

uint8_t mapper_000_ppu_read( struct mapper *map, uint16_t addr )
{
	uint8_t data = map->cartridge->chr_rom[ addr ];
	return data;
}

void mapper_000_ppu_write( struct mapper *map, uint16_t addr, uint8_t data )
{
	map->cartridge->chr_rom[ addr ] = data;
	return;
}