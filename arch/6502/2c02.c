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

// Commmunication with the CPU is done via the special registers at
// $2000-$2007 (mirrored for 0x1000)

static uint8_t
cpu_read( uint16_t addr )
{
	switch( addr & 0x2007 )
	{
		case PPUCTRL:
			break;

		case PPUMASK:
			break;

		case PPUSTATUS:
			break;

		case OAMADDR:
			break;

		case OAMDATA:
			break;

		case PPUSCROLL:
			break;

		case PPUADDR:
			break;

		case PPUDATA:
			break;

	}
}

static void
cpu_write( uint16_t addr, uint8_t data )
{
	switch( addr & 0x2007 )
	{
		case PPUCTRL:
			break;

		case PPUMASK:
			break;

		case PPUSTATUS:
			// READ ONLY
			break;

		case OAMADDR:
			break;

		case OAMDATA:
			break;

		case PPUSCROLL:
			break;

		case PPUADDR:
			break;

		case PPUDATA:
			break;

	}
}

static void
clock( )
{

}

static void
connect_bus( void *bus )
{
	ppu.bus = ( struct nesbus * ) bus;
}

struct ppu2c02 *
ppu2c02_init( )
{
	ppu.cpu_read 	= cpu_read;
	ppu.cpu_write 	= cpu_write;
	ppu.ppu_read 	= ppu_read;
	ppu.ppu_write 	= ppu_write;
	ppu.clock 		= clock;
	ppu.connect_bus = connect_bus;
	ppu.connect_cartridge = connect_cartridge;

	return &ppu;
}