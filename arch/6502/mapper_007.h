#ifndef __MAPPER_007_H__
#define __MAPPER_007_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_007_cpu_read(struct mapper *map, uint16_t addr);

void mapper_007_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_007_ppu_read(struct mapper *map, uint16_t addr);

void mapper_007_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

void mapper_007_init(struct mapper *map);

#endif /* __MAPPER_007_H__ */
