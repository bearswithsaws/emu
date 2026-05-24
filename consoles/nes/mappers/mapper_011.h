#ifndef __MAPPER_011_H__
#define __MAPPER_011_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_011_cpu_read(struct mapper *map, uint16_t addr);

void mapper_011_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_011_ppu_read(struct mapper *map, uint16_t addr);

void mapper_011_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

void mapper_011_init(struct mapper *map);

#endif /* __MAPPER_011_H__ */
