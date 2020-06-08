// 6502.c

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502.h"

static struct cpu6502 cpu = { 0 };

// Each should return how many extra clock cycles are required
// based on the caveats in the cpu docs
static uint8_t IMP( );
static uint8_t ACC( );
static uint8_t IMM( );
static uint8_t ZPG( );
static uint8_t ZPX( );
static uint8_t ZPY( );
static uint8_t REL( );
static uint8_t ABS( );
static uint8_t ABX( );
static uint8_t ABY( );
static uint8_t IND( );
static uint8_t IZX( );
static uint8_t IZY( );


static uint8_t ADC( );
static uint8_t AND( );
static uint8_t ASL( );
static uint8_t BCC( );
static uint8_t BCS( );
static uint8_t BEQ( );
static uint8_t BIT( );
static uint8_t BMI( );
static uint8_t BNE( );
static uint8_t BPL( );
static uint8_t BRK( );
static uint8_t BVC( );
static uint8_t BVS( );
static uint8_t CLC( );
static uint8_t CLD( );
static uint8_t CLI( );
static uint8_t CLV( );
static uint8_t CMP( );
static uint8_t CPX( );
static uint8_t CPY( );
static uint8_t DEC( );
static uint8_t DEX( );
static uint8_t DEY( );
static uint8_t EOR( );
static uint8_t INC( );
static uint8_t INX( );
static uint8_t INY( );
static uint8_t JMP( );
static uint8_t JSR( );
static uint8_t LDA( );
static uint8_t LDX( );
static uint8_t LDY( );
static uint8_t LSR( );
static uint8_t NOP( );
static uint8_t ORA( );
static uint8_t PHA( );
static uint8_t PHP( );
static uint8_t PLA( );
static uint8_t PLP( );
static uint8_t ROL( );
static uint8_t ROR( );
static uint8_t RTI( );
static uint8_t RTS( );
static uint8_t SBC( );
static uint8_t SEC( );
static uint8_t SED( );
static uint8_t SEI( );
static uint8_t STA( );
static uint8_t STX( );
static uint8_t STY( );
static uint8_t TAX( );
static uint8_t TAY( );
static uint8_t TSX( );
static uint8_t TXA( );
static uint8_t TXS( );
static uint8_t TYA( );
static uint8_t XXX( ); // invalid opcode

static struct instruction invalid_opcode = {"???", 0x00, &XXX, &IMP, 2 };

// This lookup table is based on the layout as described in:
// https://www.masswerk.at/6502/6502_instruction_set.html
									// c a b
static struct instruction instruction_table[3][8][8] =
{
	{
		{
			{"BRK", 0x00, &BRK, &IMP, 7 }, {"???", 0x04, &XXX, &IMP, 2 }, {"PHP", 0x08, &PHP, &IMP, 3 }, {"???", 0x0C, &XXX, &IMP, 2 }, {"BPL", 0x10, &BPL, &REL, 2 }, {"???", 0x14, &XXX, &IMP, 2 }, {"CLC", 0x18, &CLC, &IMP, 2 }, {"???", 0x1C, &XXX, &IMP, 2 } 
		},
		{
			{"JSR", 0x20, &JSR, &ABS, }, {"BIT", 0x24, &BIT, &ZPG, }, {"PLP", 0x28, &PLP, &IMP, }, {"BIT", 0x2C, &BIT, &ABS, }, {"BMI", 0x30, &BMI, &REL, }, {"???", 0x34, &XXX, &IMP, }, {"SEC", 0x38, &SEC, &IMP, }, {"???", 0x3C, &XXX, &IMP, }
		},
		{
			{"RTI", 0x40, &RTI, &IMP, }, {"???", 0x44, &XXX, &IMP, }, {"PHA", 0x48, &PHA, &IMP, }, {"JMP", 0x4C, &JMP, &ABS, }, {"BVC", 0x50, &BVC, &REL, }, {"???", 0x54, &XXX, &IMP, }, {"CLI", 0x58, &CLI, &IMP, }, {"???", 0x5C, &XXX, &IMP, }
		},
		{
			{"RTS", 0x60, &RTS, &IMP, }, {"???", 0x64, &XXX, &IMP, }, {"PLA", 0x68, &PLA, &IMP, }, {"JMP", 0x6C, &JMP, &ABS, }, {"BVS", 0x70, &BVS, &REL, }, {"???", 0x74, &XXX, &IMP, }, {"SEI", 0x78, &SEI, &IMP, }, {"???", 0x7C, &XXX, &IMP, }
		},
		{
			{"???", 0x80, &XXX, &IMP, }, {"STY", 0x84, &STY, &ZPG, }, {"DEY", 0x88, &DEY, &IMP, }, {"STY", 0x8C, &STY, &ABS, }, {"BCC", 0x90, &BCC, &REL, }, {"STY", 0x94, &STY, &ZPX, }, {"TYA", 0x98, &TYA, &IMP, }, {"???", 0x9C, &XXX, &IMP, }
		},
		{
			{"LDY", 0xA0, &LDY, &IMM, }, {"LDY", 0xA4, &LDY, &ZPG, }, {"TAY", 0xA8, &TAY, &IMP, }, {"LDY", 0xAC, &LDY, &ABS, }, {"BCS", 0xB0, &BCS, &REL, }, {"LDY", 0xB4, &LDY, &ZPX, }, {"CLV", 0xB8, &CLV, &IMP, }, {"LDY", 0xBC, &LDY, &ABX, }
		},
		{
			{"CPY", 0xC0, &CPY, &IMM, }, {"CPY", 0xC4, &CPY, &ZPG, }, {"INY", 0xC8, &INY, &IMP, }, {"CPY", 0xCC, &CPY, &ABS, }, {"BNE", 0xD0, &BNE, &REL, }, {"???", 0xD4, &XXX, &IMP, }, {"CLD", 0xD8, &CLD, &IMP, }, {"???", 0xDC, &XXX, &IMP, }
		},
		{
			{"CPX", 0xE0, &CPX, &IMM, }, {"CPX", 0xE4, &CPX, &ZPG, }, {"INX", 0xE8, &INX, &IMP, }, {"CPX", 0xEC, &CPX, &ABS, }, {"BEQ", 0xF0, &BEQ, &REL, }, {"???", 0xF4, &XXX, &IMP, }, {"SED", 0xF8, &SED, &IMP, }, {"???", 0xFC, &XXX, &IMP, }
		},
	},
	{
		{
			{"ORA", 0x01, &ORA, &IND, }, {"ORA", 0x05, &ORA, &ZPG, }, {"ORA", 0x09, &ORA, &IMM, }, {"ORA", 0x0D, &ORA, &ABS, }, {"ORA", 0x11, &ORA, &IZY, }, {"ORA", 0x15, &ORA, &ZPX, }, {"ORA", 0x19, &ORA, &ABY, }, {"ORA", 0x1D, &ORA, &ABX, }
		},
		{
			{"AND", 0x21, &AND, &IND, }, {"AND", 0x25, &AND, &ZPG, }, {"AND", 0x29, &AND, &IMM, }, {"AND", 0x2D, &AND, &ABS, }, {"AND", 0x31, &AND, &IZY, }, {"AND", 0x35, &AND, &ZPX, }, {"AND", 0x39, &AND, &ABY, }, {"AND", 0x3D, &AND, &ABX, }
		},
		{
			{"EOR", 0x41, &EOR, &IND, }, {"EOR", 0x45, &EOR, &ZPG, }, {"EOR", 0x49, &EOR, &IMM, }, {"EOR", 0x4D, &EOR, &ABS, }, {"EOR", 0x51, &EOR, &IZY, }, {"EOR", 0x55, &EOR, &ZPX, }, {"EOR", 0x59, &EOR, &ABY, }, {"EOR", 0x5D, &EOR, &ABX, }
		},
		{
			{"ADC", 0x61, &ADC, &IND, }, {"ADC", 0x65, &ADC, &ZPG, }, {"ADC", 0x69, &ADC, &IMM, }, {"ADC", 0x6D, &ADC, &ABS, }, {"ADC", 0x71, &ADC, &IZY, }, {"ADC", 0x75, &ADC, &ZPX, }, {"ADC", 0x79, &ADC, &ABY, }, {"ADC", 0x7D, &ADC, &ABX, }
		},
		{
			{"STA", 0x81, &STA, &IND, }, {"STA", 0x85, &STA, &ZPG, }, {"???", 0x89, &XXX, &IMP, }, {"STA", 0x8D, &STA, &ABS, }, {"STA", 0x91, &STA, &IZY, }, {"STA", 0x95, &STA, &ZPX, }, {"STA", 0x99, &STA, &ABY, }, {"STA", 0x9D, &STA, &ABX, }
		},
		{
			{"LDA", 0xA1, &LDA, &IND, }, {"LDA", 0xA5, &LDA, &ZPG, }, {"LDA", 0xA9, &LDA, &IMM, }, {"LDA", 0xAD, &LDA, &ABS, }, {"LDA", 0xB1, &LDA, &IZY, }, {"LDA", 0xB5, &LDA, &ZPX, }, {"LDA", 0xB9, &LDA, &ABY, }, {"LDA", 0xBD, &LDA, &ABX, }
		},
		{
			{"CMP", 0xC1, &CMP, &IND, }, {"CMP", 0xC5, &CMP, &ZPG, }, {"CMP", 0xC9, &CMP, &IMM, }, {"CMP", 0xCD, &CMP, &ABS, }, {"CMP", 0xD1, &CMP, &IZY, }, {"CMP", 0xD5, &CMP, &ZPX, }, {"CMP", 0xD9, &CMP, &ABY, }, {"CMP", 0xDD, &CMP, &ABX, }
		},
		{
			{"SBC", 0xE1, &SBC, &IND, }, {"SBC", 0xE5, &SBC, &ZPG, }, {"SBC", 0xE9, &SBC, &IMM, }, {"SBC", 0xED, &SBC, &ABS, }, {"SBC", 0xF1, &SBC, &IZY, }, {"SBC", 0xF5, &SBC, &ZPX, }, {"SBC", 0xF9, &SBC, &ABY, }, {"SBC", 0xFD, &SBC, &ABX, }
		},
	},
	{
		{
			{"???", 0x02, &XXX, &IMP, }, {"ASL", 0x06, &ASL, &ZPG, }, {"ASL", 0x0A, &ASL, &ACC, }, {"ASL", 0x0E, &ASL, &ABS, }, {"???", 0x12, &XXX, &IMP, }, {"ASL", 0x16, &ASL, &ZPX, }, {"???", 0x1A, &XXX, &IMP, }, {"ASL", 0x1E, &ASL, &ABX, }
		},
		{
			{"???", 0x22, &XXX, &IMP, }, {"ROL", 0x26, &ROL, &ZPG, }, {"ROL", 0x2A, &ROL, &ACC, }, {"ROL", 0x2E, &ROL, &ABS, }, {"???", 0x32, &XXX, &IMP, }, {"ROL", 0x36, &ROL, &ZPX, }, {"???", 0x3A, &XXX, &IMP, }, {"ROL", 0x3E, &ROL, &ABX, }
		},
		{
			{"???", 0x42, &XXX, &IMP, }, {"LSR", 0x46, &LSR, &ZPG, }, {"LSR", 0x4A, &LSR, &ACC, }, {"LSR", 0x4E, &LSR, &ABS, }, {"???", 0x52, &XXX, &IMP, }, {"LSR", 0x56, &LSR, &ZPX, }, {"???", 0x5A, &XXX, &IMP, }, {"LSR", 0x5E, &LSR, &ABX, }
		},
		{
			{"???", 0x62, &XXX, &IMP, }, {"ROR", 0x66, &ROR, &ZPG, }, {"ROR", 0x6A, &ROR, &ACC, }, {"ROR", 0x6E, &ROR, &ABS, }, {"???", 0x72, &XXX, &IMP, }, {"ROR", 0x76, &ROR, &ZPX, }, {"???", 0x7A, &XXX, &IMP, }, {"ROR", 0x7E, &ROR, &ABX, }
		},
		{
			{"???", 0x82, &XXX, &IMP, }, {"STX", 0x86, &STX, &ZPG, }, {"TXA", 0x8A, &TXA, &IMP, }, {"STX", 0x8E, &STX, &ABS, }, {"???", 0x92, &XXX, &IMP, }, {"STX", 0x96, &STX, &ZPY, }, {"TSX", 0x9A, &TSX, &IMP, }, {"???", 0x9E, &XXX, &IMP, }
		},
		{
			{"LDX", 0xA2, &LDX, &IMM, }, {"LDX", 0xA6, &LDX, &ZPG, }, {"TAX", 0xAA, &TAX, &IMP, }, {"LDX", 0xAE, &LDX, &ABS, }, {"???", 0xB2, &XXX, &IMP, }, {"LDX", 0xB6, &LDX, &ZPY, }, {"TSX", 0xBA, &TSX, &IMP, }, {"LDX", 0xBE, &LDX, &ABY, }
		},
		{
			{"???", 0xC2, &XXX, &IMP, }, {"DEC", 0xC6, &DEC, &ZPG, }, {"DEX", 0xCA, &DEX, &IMP, }, {"DEC", 0xCE, &DEC, &ABS, }, {"???", 0xD2, &XXX, &IMP, }, {"DEC", 0xD6, &DEC, &ZPX, }, {"???", 0xDA, &XXX, &IMP, }, {"DEC", 0xDE, &DEC, &ABX, }
		},
		{
			{"???", 0xE2, &XXX, &IMP, }, {"INC", 0xE6, &INC, &ZPG, }, {"NOP", 0xEA, &NOP, &IMP, }, {"INC", 0xEE, &INC, &ABS, }, {"???", 0xF2, &XXX, &IMP, }, {"INC", 0xF6, &INC, &ZPX, }, {"???", 0xFA, &XXX, &IMP, }, {"INC", 0xFE, &INC, &ABX, }
		},
	},
};

static uint8_t IMP( )
{
	// No operand
	cpu.operand = 0;
	return 0;
}

static uint8_t ACC( )
{
	// Operand is implied to be the A register
	cpu.operand = cpu.A;
	return 0;
}

static uint8_t IMM( )
{
	cpu.operand_addr = cpu.PC++;

	return 0;
}

static uint8_t ZPG( )
{
	cpu.operand_addr = ( uint16_t ) cpu.read( cpu.PC++ );

	return 0;
}

static uint8_t ZPX( )
{
	cpu.operand_addr = ( ( uint16_t ) cpu.read( cpu.PC++ ) + cpu.X ) & 0xff;

	return 0;
}

static uint8_t ZPY( )
{
	cpu.operand_addr = ( ( uint16_t ) cpu.read( cpu.PC++ ) + cpu.X ) & 0xff;

	return 0;}

static uint8_t REL( )
{
	uint16_t rel_addr;
	
	rel_addr = cpu.read( cpu.PC++ );
	
	if ( rel_addr & 0x80 )
		rel_addr |= 0xFF00;

	cpu.operand_addr = rel_addr;

	return 1;
}

static uint8_t ABS( )
{
	cpu.operand_addr = cpu.read( cpu.PC++ );
	cpu.operand_addr |= cpu.read( cpu.PC++ ) << 8;

	return 0;
}

static uint8_t ABX( )
{
	uint16_t page_check;

	cpu.operand_addr = cpu.read( cpu.PC++ );
	page_check = cpu.read( cpu.PC++ );
	cpu.operand_addr |= page_check << 8;
	cpu.operand_addr += cpu.X;

	// According to the 6502 manual, if the addition of X causes
	// this to cross a page, then add one cycle
	if ( ( cpu.operand_addr >> 8 ) & 0x00ff != page_check )
		return 1;

	return 0;
}

static uint8_t ABY( )
{
	uint16_t page_check;

	cpu.operand_addr = cpu.read( cpu.PC++ );
	page_check = cpu.read( cpu.PC++ );
	cpu.operand_addr |= page_check << 8;
	cpu.operand_addr += cpu.Y;

	// According to the 6502 manual, if the addition of Y causes
	// this to cross a page, then add one cycle
	if ( ( cpu.operand_addr >> 8 ) & 0x00ff != page_check )
		return 1;

	return 0;
}

static uint8_t IND( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.PC++ );
	ind_addr |= cpu.read( cpu.PC++ ) << 8;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;

	return 0;
}

static uint8_t IZX( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.PC++ );
	ind_addr += cpu.X;

	// Zero page wrap around
	ind_addr &= 0xff;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;

	return 0;
}

static uint8_t IZY( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.PC++ );

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;
	
	ind_addr = cpu.operand_addr + cpu.Y;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;
	
	return 0;
}


//     A + M + C -> A, C                N Z C I D V
//                                      + + + - - +
static uint8_t 
ADC( )
{
	uint16_t tmp;

	tmp = ( uint16_t )cpu.A + ( uint16_t )cpu.operand + ( uint16_t )GET_FLAG( C );
	SET_FLAG( C, ( tmp > 0xFF ) );

	// 1 + -1 = 0, c <- 1
	if ( ( ( cpu.A & 0x80 )  ^ ( cpu.operand & 0x80 ) ) && !tmp )
		SET_FLAG( C, 1 );

	// Set flags
	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );
	// See https://github.com/OneLoneCoder/olcNES/blob/master/Part%232%20-%20CPU/olc6502.cpp#L601
	SET_FLAG( V, ( ~( ( uint16_t ) cpu.A ^ ( uint16_t ) cpu.operand ) & 
				    ( ( uint16_t ) cpu.A ^ ( uint16_t ) tmp ) ) & 0x0080 );
	
	cpu.A = tmp & 0x00FF;
	
	return 0;
}

//     A AND M -> A                     N Z C I D V
//                                      + + - - - -
static uint8_t 
AND( )
{
	cpu.A = cpu.A & cpu.operand;

	// Set flags
	SET_FLAG( N, ( cpu.A | 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

//     C <- [76543210] <- 0             N Z C I D V
//                                      + + + - - -
static uint8_t 
ASL( )
{
	uint8_t tmp;

	SET_FLAG( C, ( cpu.operand & 0x80 ) >> 7 );

	tmp = cpu.operand << 1;

	if ( cpu.curr_insn->addr_mode != ACC )
		cpu.A = tmp;
	else
		cpu.write( cpu.operand_addr, ( tmp ) );

	// Set flags
	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );

	return 0;
}

static uint8_t 
BCC( )
{
	if ( !GET_FLAG( C ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

static uint8_t 
BCS( )
{
	if ( GET_FLAG( C ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

static uint8_t 
BEQ( )
{
	if ( GET_FLAG( Z ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

// bits 7 and 6 of operand are transfered to bit 7 and 6 of SR (N,V);
// the zeroflag is set to the result of operand AND accumulator.
// A AND M, M7 -> N, M6 -> V        N Z C I D V
//                                 M7 + - - - M6
static uint8_t 
BIT( )
{
	SET_FLAG( N, ( cpu.operand & 0x80 ) );
	SET_FLAG( V, ( cpu.operand & 0x40 ) );
	SET_FLAG( Z, cpu.A & cpu.operand );

	return 0;
}

static uint8_t 
BMI( )
{
	if ( GET_FLAG( N ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

static uint8_t 
BNE( )
{
	if ( !GET_FLAG( Z ) )
		cpu.PC += cpu.operand_addr;

	return 0;
}

static uint8_t 
BPL( )
{
	if ( !GET_FLAG( N ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

static uint8_t 
BRK( )
{
	// TODO: What else?
	cpu.write( cpu.SP, cpu.PC + 1 );
	cpu.SP--;
	cpu.write( cpu.SP, cpu.PC );
	cpu.SP--;

	SET_FLAG( I , 1 );
	return 0;
}

static uint8_t 
BVC( )
{
	if ( !GET_FLAG( V ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

static uint8_t 
BVS( )
{
	if ( GET_FLAG( V ) )
		cpu.PC += cpu.operand_addr;
	return 0;
}

static uint8_t 
CLC( )
{
	SET_FLAG( C, 0 );
	return 0;
}

static uint8_t 
CLD( )
{
	SET_FLAG( D, 0 );
	return 0;
}

static uint8_t 
CLI( )
{
	SET_FLAG( I, 0 );
	return 0;
}

static uint8_t 
CLV( )
{
	SET_FLAG( V, 0 );
	return 0;
}

static uint8_t 
CMP( )
{
	uint16_t tmp;

	tmp = ( uint16_t )cpu.A - ( uint16_t )cpu.operand;
	SET_FLAG( C, ( cpu.A >= cpu.operand ) ? 1 : 0  );

	// Set flags
	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !( tmp & 0xff ) ) );
	
	cpu.A = tmp & 0x00FF;

	return 0;
}

static uint8_t 
CPX( )
{
	uint8_t tmp;

	tmp = cpu.X - cpu.operand;
	
	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( C, ( cpu.X >= cpu.operand ) );

	return 0;
}

static uint8_t 
CPY( )
{
	uint8_t tmp;

	tmp = cpu.Y - cpu.operand;

	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( C, ( cpu.Y >= cpu.operand ) );
	return 0;
}

static uint8_t 
DEC( )
{
	uint8_t tmp;
	tmp = cpu.operand - 1;

	cpu.write( tmp, cpu.operand_addr );

	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );

	return 0;
}

static uint8_t 
DEX( )
{
	cpu.X--;

	SET_FLAG( N, ( cpu.X | 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

static uint8_t 
DEY( )
{
	cpu.X--;

	SET_FLAG( N, ( cpu.Y | 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

static uint8_t 
EOR( )
{
	return 0;
}

static uint8_t 
INC( )
{
	uint8_t tmp;
	tmp = cpu.operand + 1;

	cpu.write( tmp, cpu.operand_addr );

	SET_FLAG( N, ( tmp | 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );

	return 0;
}

static uint8_t 
INX( )
{
	cpu.X++;

	SET_FLAG( N, ( cpu.X | 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

static uint8_t 
INY( )
{
	cpu.Y++;

	SET_FLAG( N, ( cpu.Y | 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

static uint8_t 
JMP( )
{
	cpu.PC = cpu.operand_addr;
	return 0;
}

static uint8_t 
JSR( )
{
	cpu.write( cpu.SP, ( cpu.PC >> 8 ) & 0x00FF );
	cpu.SP--;
	cpu.write( cpu.SP, ( cpu.PC & 0x00FF ) );
	cpu.SP--;
	cpu.PC = cpu.operand_addr;

	return 0;
}

static uint8_t 
LDA( )
{
	cpu.A = cpu.operand;

	SET_FLAG( N, ( cpu.A | 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

static uint8_t 
LDX( )
{
	cpu.X = cpu.operand;

	SET_FLAG( N, ( cpu.X | 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

static uint8_t 
LDY( )
{
	cpu.Y = cpu.operand;

	SET_FLAG( N, ( cpu.Y | 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

static uint8_t 
LSR( )
{

	return 0;
}

static uint8_t 
NOP( )
{
	return 0;
}

static uint8_t 
ORA( )
{
	return 0;
}

static uint8_t 
PHA( )
{
	cpu.write( cpu.SP, cpu.A );
	cpu.SP--;
	return 0;
}

static uint8_t 
PHP( )
{
	cpu.write( cpu.SP, cpu.flags.reg );
	cpu.SP--;
	return 0;
}

static uint8_t 
PLA( )
{
	cpu.A = cpu.read( cpu.SP );
	cpu.SP++;
	return 0;
}

static uint8_t 
PLP( )
{
	cpu.flags.reg = cpu.read( cpu.SP );
	cpu.SP++;
	return 0;
}

static uint8_t 
ROL( )
{

	return 0;
}

static uint8_t 
ROR( )
{
	return 0;
}

static uint8_t 
RTI( )
{
	return 0;
}

static uint8_t 
RTS( )
{
	uint16_t tmp;

	cpu.SP++;
	tmp = cpu.read( cpu.SP );
	cpu.SP++;
	tmp |=  ( cpu.read( cpu.SP ) << 8 );
	//cpu.SP++;
	cpu.PC = tmp;
	return 0;
}

static uint8_t 
SBC( )
{
	return 0;
}

static uint8_t 
SEC( )
{
	SET_FLAG( C, 1 );
	return 0;
}

static uint8_t 
SED( )
{
	SET_FLAG( D, 1 );
	return 0;
}

static uint8_t 
SEI( )
{
	SET_FLAG( I, 1 );
	return 0;
}

static uint8_t 
STA( )
{
	cpu.write( cpu.operand_addr, cpu.A );
	return 0;
}

static uint8_t 
STX( )
{
	cpu.write( cpu.operand_addr, cpu.X );
	return 0;
}

static uint8_t 
STY( )
{
	cpu.write( cpu.operand_addr, cpu.Y );
	return 0;
}

static uint8_t 
TAX( )
{
	cpu.X = cpu.A;
	SET_FLAG( N, ( cpu.X | 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

static uint8_t 
TAY( )
{
	cpu.Y = cpu.A;
	SET_FLAG( N, ( cpu.Y | 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

static uint8_t 
TSX( )
{
	cpu.X = (uint8_t) cpu.SP;
	SET_FLAG( N, ( cpu.X | 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

static uint8_t 
TXA( )
{
	cpu.A = cpu.X;
	SET_FLAG( N, ( cpu.A | 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

static uint8_t 
TXS( )
{
	cpu.SP = ( (uint16_t) cpu.X ) & 0x00ff;

	return 0;
}

static uint8_t 
TYA( )
{
	cpu.A = cpu.Y;
	SET_FLAG( N, ( cpu.A | 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

static uint8_t 
XXX( )
{
	printf("Invalid opcode encountered\n");
	exit( 1 );
	return 0;
}

static void
print_regs( )
{
	printf("A: %02X\n", cpu.A );
	printf("X: %02X\n", cpu.X );
	printf("Y: %02X\n", cpu.Y );
	printf("SP: %04X\n", cpu.SP );
	printf("PC: %04X\n", cpu.PC );
	printf("FLAGS: %02X\n", cpu.flags.reg );
}

// TODO: read the docs fo rthe 6502 on what a reset state looks like
static void
reset( void )
{
	SET_FLAG( U, 1 );
	cpu.SP = (uint16_t)0xff;
	cpu.PC = (uint16_t)0x600;
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
	cpu.start_pc = cpu.PC;

	cpu.opcode = cpu.bus->read( cpu.PC++ );
	a = DECODE_A( cpu.opcode );
	b = DECODE_B( cpu.opcode );
	c = DECODE_C( cpu.opcode );

	// No valid 6502 instruction exists with the lowest two bits both set
	if ( c == 3 )
	{
		cpu.curr_insn = &invalid_opcode;
		// cpu.op is the actual invalid opcode
		// cpu.curr_insn contains a 0x00 placeholder value, which is incorrect
		return cpu.opcode;
	}

	cpu.curr_insn = &instruction_table[ c ][ a ][ b ];

	return cpu.opcode;
}

static uint8_t
decode( )
{

	cpu.curr_insn->addr_mode();
	if ( ( cpu.curr_insn->addr_mode != IMP ) && ( cpu.curr_insn->addr_mode != ACC ) )
		cpu.operand = cpu.read( cpu.operand_addr );
	if ( cpu.curr_insn->addr_mode == ACC )
		cpu.operand = cpu.A;
	
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
	cpu.print_regs = print_regs;

	cpu.bus = bus;

	return &cpu;
}
