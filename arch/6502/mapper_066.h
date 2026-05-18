#ifndef __MAPPER_066_H__
#define __MAPPER_066_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_066_cpu_read(struct mapper *map, uint16_t addr);

void mapper_066_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_066_ppu_read(struct mapper *map, uint16_t addr);

void mapper_066_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

void mapper_066_init(struct mapper *map);

#endif /* __MAPPER_066_H__ */
