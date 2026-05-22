#ifndef __MAPPER_019_H__
#define __MAPPER_019_H__

#include <stdint.h>

#include "mapper.h"

void mapper_019_init(struct mapper *map);

/* Called once per CPU cycle (wired to mapper->clock) for the cycle-based
 * IRQ counter. */
void mapper_019_cpu_cycle(struct mapper *map);

uint8_t mapper_019_cpu_read(struct mapper *map, uint16_t addr);

void mapper_019_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);

uint8_t mapper_019_ppu_read(struct mapper *map, uint16_t addr);

void mapper_019_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

#endif /* __MAPPER_019_H__ */
