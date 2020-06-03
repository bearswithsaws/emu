#ifndef __NESBUS_H__
#define __NESBUS_H__

#include <stdint.h>

struct nesbus;

typedef uint8_t ( *fp_read ) ( uint16_t addr );
typedef void ( *fp_write ) ( uint16_t addr, uint8_t data );
typedef void ( *fp_load) ( uint16_t addr, uint8_t *data, uint16_t len );
struct nesbus
{
	fp_read read;
	fp_write write;
	fp_load load;
};

struct nesbus * nesbus_init( );

#endif /* __NESBUS_H__ */