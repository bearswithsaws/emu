#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "emu_config.h"
#include "6502.h"
#include "nesbus.h"

#include "debug.h"

//uint8_t SAMPLE_BIN[] = {0xa9,0x01,0x8d,0x00,0x02,0xa9,0x05,0x8d,0x01,0x02,0xa9,0x08,0x8d,0x02,0x02};
uint8_t SAMPLE_BIN[] = { 0xa2, 0x00, 0xa0, 0x00, 0x8a, 0x99, 0x00, 0x02, 0x48, 0xe8, 0xc8, 0xc0, 0x10, 0xd0, 0xf5, 0x68, 0x99, 0x00, 0x02, 0xc8, 0xc0, 0x20, 0xd0, 0xf7 };

static struct nesbus *bus;
static struct cpu6502 *cpu;

int
main(int argc, char *argv[] )
{
	( void ) argc;
	( void ) argv;

	uint8_t buf[0x100];

	printf( "Emu version %d.%d\n", emu_VERSION_MAJOR, emu_VERSION_MINOR );
	bus = nesbus_init( );
	cpu = cpu6502_init( bus );
	cpu->reset();
	// Hack for now to "load" mem
	bus->load( 0, SAMPLE_BIN, sizeof( SAMPLE_BIN ) );
	printf("bus: %p\n", bus);
	printf("cpu: %p cpu->bus: %p\n", cpu, cpu->bus);
	printf("bus read[$100]: %02x\n(write val on bus)\n", bus->read( 0x100 ) );
	bus->write( 0x100, 0x41 );
	printf("cpu read[$100]: %02x\n", cpu->read( 0x100 ) );
	do
	{
		cpu->fetch( );
		cpu->decode( );
		printf("%04x: %02x %s %04x\n",
				cpu->start_pc,
				cpu->op,
				cpu->curr_insn->mnem,
				cpu->operand);

		cpu->execute( );
		bus->debug_read(0x100, buf, 0x100);
		hex_dump( buf, 0x100 );
	} while ( cpu->pc < sizeof( SAMPLE_BIN ) );

	return 0;
}