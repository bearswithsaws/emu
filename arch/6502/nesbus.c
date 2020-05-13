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
	if ( addr < 0x800 )
	{
		return ram[ addr ];	
	}
	else if ( ( addr >= 0x800 ) && ( addr < 0x2000 ) )
	{
		// mirroring
		return ram[ addr & 0x7ff ];
	}
	else if ( (addr >= 0x2000 ) && ( addr < 0x3fff ) )
	{
		// So wrong but need to implement PPU later
		//return ram[ addr ];
		return 0xff;
	}
	else if ( ( addr == 0x3fff ) )
	{
		// ppu read
		printf("PPU READ\n");
	}
	else if ( ( addr >= 0x4000 ) && ( addr <= 0x4017 ) )
	{
		// apu read
		printf("APU READ\n");
	}
	else
	{
		// remainder of reads like cartridge and open bus?
		return bus.map->read( bus.map, addr );
	}
}

static void
write( uint16_t addr, uint8_t data )
{
	if ( addr < 0x800 )
	{
		ram[ addr ] = data;	
	}
	else if ( ( addr >= 0x800 ) && ( addr < 0x2000 ) )
	{
		// mirroring
		ram[ addr & 0x7ff ] = data;
	}
	else if ( (addr >= 0x2000 ) && ( addr < 0x3fff ) )
	{
		// So wrong but need to implement PPU later
		ram[ addr ] = data;
	}
	else if ( ( addr == 0x3fff ) )
	{
		// ppu write
		printf("PPU WRITE\n");
	}
	else if ( ( addr >= 0x4000 ) && ( addr <= 0x4017 ) )
	{
		// apu write
		printf("APU WRITE\n");
	}
	else
	{
		// remainder of reads like cartridge and open bus?
		bus.map->write( bus.map, addr, data );
	}
	return;
}

static struct mapper * 
connect_cartridge( struct nes_cartridge *cartridge )
{
	struct mapper *map;
	map = mapper_init( &bus, cartridge );
	bus.map = map;
}

static void
load( uint16_t addr, uint8_t *data, uint16_t len )
{
	memcpy( &ram[ addr ], data, len );
}

static uint8_t *
debug_read( uint16_t offset, uint8_t *buf, uint16_t len )
{
	for (int i = 0; i < len; i++)
	{
		buf[i] = bus.read( offset + i );
	}
	return buf;
}

struct nesbus *
nesbus_init( )
{
	bus.read 	= read;
	bus.write 	= write;
	bus.connect_cartridge = connect_cartridge;
	bus.load 	= load;
	bus.debug_read = debug_read;

	return &bus;
}