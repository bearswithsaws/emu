#include "mapper.h"

#include "mapper_000.h"
#include "mapper_001.h"
#include "mapper_002.h"
#include "mapper_003.h"

static struct mapper map = { 0 };

struct mapper *
mapper_init( struct nesbus *bus, struct nes_cartridge *cartridge )
{
	// Not sure if we need the entire cartridge or just values from it.
	// Saving both for now.
	map.cartridge = cartridge;
	map.mapper_id = cartridge->mapper_id;
	map.num_prg_rom = cartridge->hdr->prg_rom_size;
	map.num_chr_rom = cartridge->hdr->chr_rom_size;

	switch( map.mapper_id )
	{
		case 0:
			map.read = mapper_000_read;
			map.write = mapper_000_write;
			break;
		case 1:
			map.read = mapper_001_read;
			map.write = mapper_001_write;
			break;
		case 2:
			map.read = mapper_002_read;
			map.write = mapper_002_write;
			break;
		case 3:
			map.read = mapper_003_read;
			map.write = mapper_003_write;
			break;

	}

	map.bus = bus;

	return &map;
}