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
	uint8_t data;
	printf("ADDR %04x\n", addr);

	switch( addr & 0x2007 )
	{
		case PPUCTRL:
			// WRITE ONLY
			printf("PPUCTRL is write only\n");
			exit(1);
			break;

		case PPUMASK:
			// WRITE ONLY
			printf("PPUMASK is write only\n");
			exit(1);
			break;

		case PPUSTATUS:
			//hack
			ppu.ppustatus.vblank_started  = 1;
			// The act of reading this register resets vblank
			data =  ppu.ppustatus.reg;
			ppu.ppustatus.vblank_started  = 0;
			break;

		case OAMADDR:
			// Reading returns open bus...
			break;

		case OAMDATA:
			// Return data at OMAMADDR
			break;

		case PPUSCROLL:
			//
			break;

		case PPUADDR:
			// Reading returns open bus...
			break;

		case PPUDATA:
			// Return data at PPUADDR
			break;

	}

	return data;
}

static void
cpu_write( uint16_t addr, uint8_t data )
{
	printf("ADDR %04x DATA %02x\n", addr, data);
	switch( addr & 0x2007 )
	{
		case PPUCTRL:
			ppu.ppuctrl.reg = data;
			break;

		case PPUMASK:
			ppu.ppumask.reg = data;
			break;

		case PPUSTATUS:
			// READ ONLY
			printf("PPUSTATUS is read only\n");
			exit(1);
			break;

		case OAMADDR:
			break;

		case OAMDATA:
			break;

		case PPUSCROLL:
			ppu.ppuscroll = data;
			break;

		case PPUADDR:
			if ( ppu.ppuaddr_latch == 0 )
			{
				ppu.ppuaddr = ( data << 8 );
				ppu.ppuaddr_latch = 1;
				printf("PPUADDR: %04x\n", ppu.ppuaddr);
			}
			else
			{
				ppu.ppuaddr |= data;
				ppu.ppuaddr_latch = 0;
				printf("PPUADDR: %04x\n", ppu.ppuaddr);
			}
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

static void
reset( void )
{
	ppu.ppuaddr_latch = 0;

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
	ppu.reset 		= reset;
	ppu.connect_cartridge = connect_cartridge;

	return &ppu;
}