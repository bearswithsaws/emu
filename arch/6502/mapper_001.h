#ifndef __MAPPER_001_H__
#define __MAPPER_001_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_001_cpu_read( struct mapper *map, uint16_t addr );

void mapper_001_cpu_write( struct mapper *map, uint16_t addr, uint8_t data );

uint8_t mapper_001_ppu_read( struct mapper *map, uint16_t addr );

void mapper_001_ppu_write( struct mapper *map, uint16_t addr, uint8_t data );

#endif /* __MAPPER_001_H__ */