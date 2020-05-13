#include "2c02.h"

static struct ppu2c02 ppu = { 0 };

static void 
connect_cartridge( struct nes_cartridge *cartridge )
{
	ppu.cart = cartridge;
}

static uint8_t
ppu_read( uint16_t addr )
{

}

static void
ppu_write( uint16_t addr, uint8_t data )
{

}

static uint8_t
cpu_read( uint16_t addr )
{

}

static void
cpu_write( uint16_t addr, uint8_t data )
{

}

static void
clock( )
{

}

struct ppu2c02 *
ppu2c02_init( struct nesbus *bus )
{
	ppu.cpu_read 	= cpu_read;
	ppu.cpu_write 	= cpu_write;
	ppu.ppu_read 	= ppu_read;
	ppu.ppu_write 	= ppu_write;
	ppu.clock 		= clock;
	ppu.connect_cartridge = connect_cartridge;
	ppu.bus = bus;

	return &ppu;
}