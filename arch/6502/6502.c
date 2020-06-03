// 6502.c

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502.h"

static struct cpu6502 cpu = { 0 };

uint8_t 
ADC( )
{
	return 0;
}

uint8_t 
AND( )
{
	return 0;
}

uint8_t 
ASL( )
{
	return 0;
}

uint8_t 
BCC( )
{
	return 0;
}

uint8_t 
BCS( )
{
	return 0;
}

uint8_t 
BEQ( )
{
	return 0;
}

uint8_t 
BIT( )
{
	return 0;
}

uint8_t 
BMI( )
{
	return 0;
}

uint8_t 
BNE( )
{
	return 0;
}

uint8_t 
BPL( )
{
	return 0;
}

uint8_t 
BRK( )
{
	return 0;
}

uint8_t 
BVC( )
{
	return 0;
}

uint8_t 
BVS( )
{
	return 0;
}

uint8_t 
CLC( )
{
	return 0;
}

uint8_t 
CLD( )
{
	return 0;
}

uint8_t 
CLI( )
{
	return 0;
}

uint8_t 
CLV( )
{
	return 0;
}

uint8_t 
CMP( )
{
	return 0;
}

uint8_t 
CPX( )
{
	return 0;
}

uint8_t 
CPY( )
{
	return 0;
}

uint8_t 
DEC( )
{
	return 0;
}

uint8_t 
DEX( )
{
	return 0;
}

uint8_t 
DEY( )
{
	return 0;
}

uint8_t 
EOR( )
{
	return 0;
}

uint8_t 
INC( )
{
	return 0;
}

uint8_t 
INX( )
{
	return 0;
}

uint8_t 
INY( )
{
	return 0;
}

uint8_t 
JMP( )
{
	return 0;
}

uint8_t 
JSR( )
{
	return 0;
}

uint8_t 
LDA( )
{
	return 0;
}

uint8_t 
LDX( )
{
	return 0;
}

uint8_t 
LDY( )
{
	return 0;
}

uint8_t 
LSR( )
{
	return 0;
}

uint8_t 
NOP( )
{
	return 0;
}

uint8_t 
ORA( )
{
	return 0;
}

uint8_t 
PHA( )
{
	return 0;
}

uint8_t 
PHP( )
{
	return 0;
}

uint8_t 
PLA( )
{
	return 0;
}

uint8_t 
PLP( )
{
	return 0;
}

uint8_t 
ROL( )
{
	return 0;
}

uint8_t 
ROR( )
{
	return 0;
}

uint8_t 
RTI( )
{
	return 0;
}

uint8_t 
RTS( )
{
	return 0;
}

uint8_t 
SBC( )
{
	return 0;
}

uint8_t 
SEC( )
{
	return 0;
}

uint8_t 
SED( )
{
	return 0;
}

uint8_t 
SEI( )
{
	return 0;
}

uint8_t 
STA( )
{
	return 0;
}

uint8_t 
STX( )
{
	return 0;
}

uint8_t 
STY( )
{
	return 0;
}

uint8_t 
TAX( )
{
	return 0;
}

uint8_t 
TAY( )
{
	return 0;
}

uint8_t 
TSX( )
{
	return 0;
}

uint8_t 
TXA( )
{
	return 0;
}

uint8_t 
TXS( )
{
	return 0;
}

uint8_t 
TYA( )
{
	return 0;
}

uint8_t 
XXX( )
{
	return 0;
}


static uint8_t
read( uint16_t addr )
{
	return cpu.bus->read( addr );	
}

static void
write( uint16_t addr, uint8_t data )
{
	cpu.bus->write( addr, data );
	return;
}

static uint8_t fetch( )
{
	struct instruction *insn;
	uint16_t operand;
	uint8_t a, b, c;

	cpu.op = cpu.bus->read( cpu.pc++ );
	a = DECODE_A( cpu.op );
	b = DECODE_B( cpu.op );
	c = DECODE_C( cpu.op );
	printf("op: %02x a: %d b: %d c: %d\n", cpu.op, a, b, c );
	insn = &instruction_table[ c ][ a ][ b ];
	//operand = cpu.read( pc, )
	cpu.pc += insn->addr_mode;
	printf("%s\n", insn->mnem );
	return cpu.op;
}

struct cpu6502 *
cpu6502_init( struct nesbus *bus )
{
	cpu.read 	= read;
	cpu.write 	= write;
	cpu.fetch 	= fetch;

	cpu.bus = bus;

	return &cpu;
}
