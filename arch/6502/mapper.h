#ifndef __MAPPER_H__
#define __MAPPER_H__

#include <stdint.h>

#include "cartridge.h"
#include "nesbus.h"

struct mapper;

typedef uint8_t ( *fp_mapper_read ) ( struct mapper *map, uint16_t addr );
typedef void ( *fp_mapper_write ) ( struct mapper *map,  uint16_t addr, uint8_t data );

struct mapper
{
	uint8_t mapper_id;
	fp_mapper_read read;
	fp_mapper_write write;
	struct nesbus *bus;
	struct nes_cartridge *cartridge;
	uint8_t num_prg_rom;
	uint8_t num_chr_rom;
};

struct mapper * mapper_init( struct nesbus *bus, struct nes_cartridge *cartridge );

#endif /* __MAPPER_H__ */