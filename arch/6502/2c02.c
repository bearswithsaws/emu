#include "2c02.h"

#include "debug.h"

static struct ppu2c02 ppu = { 0 };

static void 
connect_cartridge( struct nes_cartridge *cartridge )
{
	ppu.cart = cartridge;
}

static uint16_t
nametable_mirror( uint16_t addr )
{
	uint16_t mirror_addr;

	//      (0,0)     (256,0)     (511,0)
	//        +-----------+-----------+
	//        |           |           |
	//        |           |           |
	//        |   $2000   |   $2400   |
	//        |           |           |
	//        |           |           |
	// (0,240)+-----------+-----------+(511,240)
	//        |           |           |
	//        |           |           |
	//        |   $2800   |   $2C00   |
	//        |           |           |
	//        |           |           |
	//        +-----------+-----------+
	//      (0,479)   (256,479)   (511,479)
	mirror_addr = addr - 0x2000;
	if ( ppu.cart->hdr->flags6.mirroring == 1 )
	{
		// horizontal
	}
	else
	{
		// vertical
	}

	return mirror_addr;
}

static uint8_t
ppu_read( uint16_t addr )
{
	if ( addr < 0x2000 )
	{
		return ppu.cart->ppu_read( ppu.cart, addr );
	}
	else if ( addr >= 0x2000 && addr <= 0x3eff )
	{
		printf("nametable read %04x\n", addr );
		return ppu.nametable[ nametable_mirror( addr ) ];

	}
	else if ( addr >= 0x3f00 && addr <= 0x3fff )
	{
		//palette
		printf("Palette READ\n");
		return ppu.palette_table[ addr & 0x1f ];
	}
	else if ( addr >= 0x4000  )
	{
		// [0x4000, 0xFFFF]
		// 	These addresses are mirrors of the the of the
		// memory space [0, 0x3FFF], that is, any address
		// that falls in here, it is accessed by data[addr & 0x3FFF].
		return ppu_read( addr & 0x3fff );
	}
}

static void
ppu_write( uint16_t addr, uint8_t data )
{
	if ( addr < 0x2000 )
	{
		ppu.cart->ppu_write( ppu.cart, addr, data );
	}
	else if ( addr >= 0x2000 && addr <= 0x3eff )
	{
		printf("nametable write %04x : %02x\n", nametable_mirror( addr ), data );
		ppu.nametable[ nametable_mirror( addr ) ] = data;
		dump_nametable( ppu.nametable );
	}
	else if ( addr >= 0x3f00 && addr <= 0x3fff )
	{
		//palette
		printf("Palette WRITE %04x %02x\n", addr, data);
		ppu.palette_table[ addr & 0x1f ] = data;
	}
	else if ( addr >= 0x4000  )
	{
		// [0x4000, 0xFFFF]
		// 	These addresses are mirrors of the the of the
		// memory space [0, 0x3FFF], that is, any address
		// that falls in here, it is accessed by data[addr & 0x3FFF].
		return ppu_write( addr & 0x3fff, data );
	}
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
			// Reads are delayed
			printf("PPU READ\n");
			// auto increment based on ctrl register
			ppu.ppuaddr += ( ppu.ppuctrl.vram_addr_increment ) ? 32 : 1;
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
			ppu_write( ppu.ppuaddr, data );
			// auto increment based on ctrl register
			ppu.ppuaddr += ( ppu.ppuctrl.vram_addr_increment ) ? 32 : 1;
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