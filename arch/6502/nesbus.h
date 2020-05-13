#ifndef __NESBUS_H__
#define __NESBUS_H__

#include <stdint.h>

#include "cartridge.h"


struct nesbus;

typedef uint8_t ( *fp_read ) ( uint16_t addr );
typedef void ( *fp_write ) ( uint16_t addr, uint8_t data );
typedef void ( *fp_clock ) ( void );

typedef uint8_t *( *fp_debug_read) ( uint16_t offset, uint8_t *buf, uint16_t len );

struct nesbus
{
	fp_read read;
	fp_write write;
	fp_clock clock;
	fp_connect_cartridge connect_cartridge;
	fp_debug_read debug_read;
	struct nes_cartridge *cart;
};

struct nesbus * nesbus_init( );

#endif /* __NESBUS_H__ */