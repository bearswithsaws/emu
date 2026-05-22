#ifndef __MAPPER_071_H__
#define __MAPPER_071_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_071_cpu_read(struct mapper *map, uint16_t addr);

void mapper_071_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_071_ppu_read(struct mapper *map, uint16_t addr);

void mapper_071_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

void mapper_071_init(struct mapper *map);

#endif /* __MAPPER_071_H__ */
