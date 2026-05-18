#ifndef __MAPPER_009_H__
#define __MAPPER_009_H__

#include <stdint.h>
#include "mapper.h"

void    mapper_009_init(struct mapper *map);
uint8_t mapper_009_cpu_read(struct mapper *map, uint16_t addr);
void    mapper_009_cpu_write(struct mapper *map, uint16_t addr, uint8_t data);
uint8_t mapper_009_ppu_read(struct mapper *map, uint16_t addr);
void    mapper_009_ppu_write(struct mapper *map, uint16_t addr, uint8_t data);

#endif /* __MAPPER_009_H__ */
