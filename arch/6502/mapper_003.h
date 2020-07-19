#ifndef __MAPPER_003_H__
#define __MAPPER_003_H__

#include <stdint.h>

#include "mapper.h"

uint8_t mapper_003_cpu_read(struct mapper *map, uint16_t addr);

void mapper_003_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_003_ppu_read(struct mapper *map, uint16_t addr);

void mapper_003_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

#endif /* __MAPPER_003_H__ */