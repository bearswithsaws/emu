#include "mapper_001.h"

uint8_t mapper_001_cpu_read( struct mapper *map, uint16_t addr )
{
	return 0;
}

void mapper_001_cpu_write( struct mapper *map, uint16_t addr, uint8_t data )
{
	return;
}

uint8_t mapper_001_ppu_read( struct mapper *map, uint16_t addr )
{
	uint8_t data = 0;
	return data;
}

void mapper_001_ppu_write( struct mapper *map, uint16_t addr, uint8_t data )
{

	return;
}