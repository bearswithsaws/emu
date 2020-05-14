#include "mapper_003.h"

uint8_t mapper_003_cpu_read( struct mapper *map, uint16_t addr )
{
	return 0;
}

void mapper_003_cpu_write( struct mapper *map, uint16_t addr, uint8_t data )
{
	return;
}

uint8_t mapper_003_ppu_read( struct mapper *map, uint16_t addr )
{
	uint8_t data = 0;
	return data;
}

void mapper_003_ppu_write( struct mapper *map, uint16_t addr, uint8_t data )
{

	return;
}