#ifndef __2C02_H__
#define __2C02_H__

#include <stdint.h>
#include "nesbus.h"
#include "cartridge.h"

struct ppu2c02;

typedef uint8_t ( *fp_ppu_read ) ( uint16_t addr );
typedef void ( *fp_ppu_write ) ( uint16_t addr, uint8_t data );
typedef uint8_t ( *fp_cpu_read ) ( uint16_t addr );
typedef void ( *fp_cpu_write ) ( uint16_t addr, uint8_t data );

struct ppu2c02
{
	fp_ppu_read 	ppu_read;
	fp_ppu_write 	ppu_write;
	fp_cpu_read 	cpu_read;
	fp_cpu_write 	cpu_write;
	fp_connect_cartridge connect_cartridge;
	struct nes_cartridge *cart;
	struct nesbus *bus;
};

struct ppu2c02 * ppu2c02_init( struct nesbus *bus );


#endif /* __2C02_H__ */