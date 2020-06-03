// NES Bus

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nesbus.h"

static struct nesbus bus = { 0 };

#define NES_RAM_SIZE ( 64 * 1024 )

static uint8_t ram[ NES_RAM_SIZE ] = { 0 };

static uint8_t
read( uint16_t addr )
{
	return ram[ addr ];	
}

static void
write( uint16_t addr, uint8_t data )
{
	ram[ addr ] = data;
	return;
}

static void
load( uint16_t addr, uint8_t *data, uint16_t len )
{
	memcpy( &ram[ addr ], data, len );
}

struct nesbus *
nesbus_init( )
{
	bus.read = read;
	bus.write = write;
	bus.load = load;

	return &bus;
}