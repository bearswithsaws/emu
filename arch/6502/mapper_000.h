#ifndef __MAPPER_000_H__
#define __MAPPER_000_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_000_read( struct mapper *map, uint16_t addr );

void mapper_000_write( struct mapper *map, uint16_t addr, uint8_t data );

#endif /* __MAPPER_000_H__ */