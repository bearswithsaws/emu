#ifndef __NESBUS_H__
#define __NESBUS_H__

#include <stdint.h>

#include "mapper.h"
#include "cartridge.h"


struct nesbus;

typedef uint8_t ( *fp_read ) ( uint16_t addr );
typedef void ( *fp_write ) ( uint16_t addr, uint8_t data );
typedef void ( *fp_load) ( uint16_t addr, uint8_t *data, uint16_t len );
typedef uint8_t *( *fp_debug_read) ( uint16_t offset, uint8_t *buf, uint16_t len );
typedef struct mapper * ( *fp_connect_cartridge ) ( struct nes_cartridge *cartridge );
struct nesbus
{
	fp_read read;
	fp_write write;
	fp_connect_cartridge connect_cartridge;
	fp_load load;
	fp_debug_read debug_read;
	struct mapper *map;
};

struct nesbus * nesbus_init( );

#endif /* __NESBUS_H__ */