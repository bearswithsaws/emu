// 6502.c

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502.h"

static struct cpu6502 cpu = { 0 };

uint8_t IMP( )
{
	// No operand
	cpu.operand = 0;
	return 0;
}

uint8_t ACC( )
{
	// Operand is implied to be the A register
	cpu.operand = cpu.a;
	return 0;
}

uint8_t IMM( )
{
	cpu.operand_addr = cpu.pc++;

	return 0;
}

uint8_t ZPG( )
{
	cpu.operand_addr = ( uint16_t ) cpu.read( cpu.pc++ );

	return 0;
}

uint8_t ZPX( )
{
	cpu.operand_addr = ( ( uint16_t ) cpu.read( cpu.pc++ ) + cpu.x ) & 0xff;

	return 0;
}

uint8_t ZPY( )
{
	cpu.operand_addr = ( ( uint16_t ) cpu.read( cpu.pc++ ) + cpu.x ) & 0xff;

	return 0;}

uint8_t REL( )
{
	uint16_t rel_addr;
	
	rel_addr = cpu.read( cpu.pc++ );
	
	if ( rel_addr & 0x80 )
		rel_addr |= 0xFF00;

	cpu.operand_addr = rel_addr;

	return 1;
}

uint8_t ABS( )
{
	cpu.operand_addr = cpu.read( cpu.pc++ );
	cpu.operand_addr |= cpu.read( cpu.pc++ ) << 8;

	return 0;
}

uint8_t ABX( )
{
	uint16_t page_check;

	cpu.operand_addr = cpu.read( cpu.pc++ );
	page_check = cpu.read( cpu.pc++ );
	cpu.operand_addr |= page_check << 8;
	cpu.operand_addr += cpu.x;

	// According to the 6502 manual, if the addition of X causes
	// this to cross a page, then add one cycle
	if ( ( cpu.operand_addr >> 8 ) & 0x00ff != page_check )
		return 1;

	return 0;
}

uint8_t ABY( )
{
	uint16_t page_check;

	cpu.operand_addr = cpu.read( cpu.pc++ );
	page_check = cpu.read( cpu.pc++ );
	cpu.operand_addr |= page_check << 8;
	cpu.operand_addr += cpu.y;

	// According to the 6502 manual, if the addition of Y causes
	// this to cross a page, then add one cycle
	if ( ( cpu.operand_addr >> 8 ) & 0x00ff != page_check )
		return 1;

	return 0;
}

uint8_t IND( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.pc++ );
	ind_addr |= cpu.read( cpu.pc++ ) << 8;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;

	return 0;
}

uint8_t IZX( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.pc++ );
	ind_addr += cpu.x;

	// Zero page wrap around
	ind_addr &= 0xff;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;

	return 0;
}

uint8_t IZY( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.pc++ );

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;
	
	ind_addr = cpu.operand_addr + cpu.y;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;
	
	return 0;
}


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
	cpu.x = ( uint8_t ) cpu.operand;
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


// TODO: read the docs fo rthe 6502 on what a reset state looks like
static void
reset( void )
{
	cpu.sp = (uint16_t)0x1fc;
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

static uint8_t
fetch( )
{
	uint16_t operand;
	uint8_t a, b, c;

	// DEBUG
	cpu.start_pc = cpu.pc;

	cpu.op = cpu.bus->read( cpu.pc++ );
	a = DECODE_A( cpu.op );
	b = DECODE_B( cpu.op );
	c = DECODE_C( cpu.op );

	// No valid 6502 instruction exists with the lowest two bits both set
	if ( c == 3 )
	{
		cpu.curr_insn = &invalid_opcode;
		// cpu.op is the actual invalid opcode
		// cpu.curr_insn contains a 0x00 placeholder value, which is incorrect
		return cpu.op;
	}

	cpu.curr_insn = &instruction_table[ c ][ a ][ b ];

	return cpu.op;
}

static uint8_t
decode( )
{

	cpu.curr_insn->addr_mode();
	if ( ( cpu.curr_insn->addr_mode != IMP ) && ( cpu.curr_insn->addr_mode != ACC ) )
		cpu.operand = cpu.read( cpu.operand_addr );
	
	return 0;
}

static uint8_t
execute( )
{
	cpu.curr_insn->execute( );
}

struct cpu6502 *
cpu6502_init( struct nesbus *bus )
{
	cpu.reset 	= reset;
	cpu.read 	= read;
	cpu.write 	= write;
	cpu.fetch 	= fetch;
	cpu.decode 	= decode;
	cpu.execute	= execute;

	cpu.bus = bus;

	return &cpu;
}
