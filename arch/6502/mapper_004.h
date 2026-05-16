#ifndef __MAPPER_004_H__
#define __MAPPER_004_H__

#include <stdint.h>

#include "mapper.h"

void mapper_004_init(struct mapper *map);

uint8_t mapper_004_cpu_read(struct mapper *map, uint16_t addr);

void mapper_004_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_004_ppu_read(struct mapper *map, uint16_t addr);

void mapper_004_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

#endif /* __MAPPER_004_H__ */
