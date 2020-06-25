#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "emu_config.h"
#include "6502.h"
#include "nesbus.h"
#include "2c02.h"
#include "cartridge.h"
#include "gui.h"

#include "debug.h"


static struct nesbus *bus;
static struct cpu6502 *cpu;
static struct ppu2c02 *ppu;


int
main(int argc, char *argv[] )
{
	uint32_t INST_CNT = -1;

	struct nes_cartridge *cartridge;

	uint8_t buf[0x100];
	uint8_t cycles = 0;

	uint64_t tick_count = 0;

	gui_init();

	printf( "Emu version %d.%d\n", emu_VERSION_MAJOR, emu_VERSION_MINOR );
	
	if ( argc > 1 )
	{
		cartridge = load_rom( argv[1] );
	}
	else
	{
		cartridge = load_rom( "/mnt/c/work/test/roms/nestest.nes" );
		//cartridge = load_rom( "/mnt/c/work/test/roms/donkeykong.nes" );
	}
	cartridge_info( cartridge );

	cpu = cpu6502_init( );
	ppu = ppu2c02_init( );
	bus = nesbus_init( cpu, ppu );
	bus->connect_cartridge( cartridge );


	printf("End of the cartridge:\n");
	bus->debug_read(0xffff-0xf, buf, 0x10);
	hex_dump(buf, 0x10);

	printf("PC:\n");
	bus->debug_read(cpu->PC, buf, 0x20);
	hex_dump( buf, 0x20 );
	cpu->reset();

	do
	{
		// PPU runs 3X faster than CPU
		//http://nemulator.com/files/nes_emu.txt (timing section)
		ppu->clock();
		if ( tick_count % 3 == 0 )
			cpu->clock();

	} while ( INST_CNT-- );

	return 0;
}