#ifndef __MAPPER_069_H__
#define __MAPPER_069_H__

#include <stdint.h>

#include "mapper.h"

void mapper_069_init(struct mapper *map);

void mapper_069_clock(struct mapper *map);

uint8_t mapper_069_cpu_read(struct mapper *map, uint16_t addr);

void mapper_069_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_069_ppu_read(struct mapper *map, uint16_t addr);

void mapper_069_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

#endif /* __MAPPER_069_H__ */
