#ifndef __MAPPER_000_H__
#define __MAPPER_000_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_000_cpu_read(struct mapper *map, uint16_t addr);

void mapper_000_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_000_ppu_read(struct mapper *map, uint16_t addr);

void mapper_000_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

#endif /* __MAPPER_000_H__ */