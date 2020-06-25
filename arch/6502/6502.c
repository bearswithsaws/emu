// 6502.c

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502.h"

#include "debug.h"

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
static uint8_t IDX( );
static uint8_t IDY( );


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
			{"JSR", 0x20, &JSR, &ABS, 6 }, {"BIT", 0x24, &BIT, &ZPG, 3 }, {"PLP", 0x28, &PLP, &IMP, 4 }, {"BIT", 0x2C, &BIT, &ABS, 4 }, {"BMI", 0x30, &BMI, &REL, 2 }, {"???", 0x34, &XXX, &IMP, 2 }, {"SEC", 0x38, &SEC, &IMP, 2 }, {"???", 0x3C, &XXX, &IMP, 2 }
		},
		{
			{"RTI", 0x40, &RTI, &IMP, 6 }, {"???", 0x44, &XXX, &IMP, 2 }, {"PHA", 0x48, &PHA, &IMP, 3 }, {"JMP", 0x4C, &JMP, &ABS, 3 }, {"BVC", 0x50, &BVC, &REL, 2 }, {"???", 0x54, &XXX, &IMP, 2 }, {"CLI", 0x58, &CLI, &IMP, 2 }, {"???", 0x5C, &XXX, &IMP, 2 }
		},
		{
			{"RTS", 0x60, &RTS, &IMP, 6 }, {"???", 0x64, &XXX, &IMP, 2 }, {"PLA", 0x68, &PLA, &IMP, 4 }, {"JMP", 0x6C, &JMP, &ABS, 5 }, {"BVS", 0x70, &BVS, &REL, 2 }, {"???", 0x74, &XXX, &IMP, 2 }, {"SEI", 0x78, &SEI, &IMP, 2 }, {"???", 0x7C, &XXX, &IMP, 2 }
		},
		{
			{"???", 0x80, &XXX, &IMP, 2 }, {"STY", 0x84, &STY, &ZPG, 3 }, {"DEY", 0x88, &DEY, &IMP, 2 }, {"STY", 0x8C, &STY, &ABS, 4 }, {"BCC", 0x90, &BCC, &REL, 2 }, {"STY", 0x94, &STY, &ZPX, 4 }, {"TYA", 0x98, &TYA, &IMP, 2 }, {"???", 0x9C, &XXX, &IMP, 2 }
		},
		{
			{"LDY", 0xA0, &LDY, &IMM, 2 }, {"LDY", 0xA4, &LDY, &ZPG, 3 }, {"TAY", 0xA8, &TAY, &IMP, 2 }, {"LDY", 0xAC, &LDY, &ABS, 4 }, {"BCS", 0xB0, &BCS, &REL, 2 }, {"LDY", 0xB4, &LDY, &ZPX, 4 }, {"CLV", 0xB8, &CLV, &IMP, 2 }, {"LDY", 0xBC, &LDY, &ABX, 4 }
		},
		{
			{"CPY", 0xC0, &CPY, &IMM, 2 }, {"CPY", 0xC4, &CPY, &ZPG, 3 }, {"INY", 0xC8, &INY, &IMP, 2 }, {"CPY", 0xCC, &CPY, &ABS, 4 }, {"BNE", 0xD0, &BNE, &REL, 2 }, {"???", 0xD4, &XXX, &IMP, 2 }, {"CLD", 0xD8, &CLD, &IMP, 2 }, {"???", 0xDC, &XXX, &IMP, 2 }
		},
		{
			{"CPX", 0xE0, &CPX, &IMM, 2 }, {"CPX", 0xE4, &CPX, &ZPG, 3 }, {"INX", 0xE8, &INX, &IMP, 2 }, {"CPX", 0xEC, &CPX, &ABS, 4 }, {"BEQ", 0xF0, &BEQ, &REL, 2 }, {"???", 0xF4, &XXX, &IMP, 2 }, {"SED", 0xF8, &SED, &IMP, 2 }, {"???", 0xFC, &XXX, &IMP, 2 }
		},
	},
	{
		{
			{"ORA", 0x01, &ORA, &IDX, 6 }, {"ORA", 0x05, &ORA, &ZPG, 3 }, {"ORA", 0x09, &ORA, &IMM, 2 }, {"ORA", 0x0D, &ORA, &ABS, 4 }, {"ORA", 0x11, &ORA, &IDY, 5 }, {"ORA", 0x15, &ORA, &ZPX, 2 }, {"ORA", 0x19, &ORA, &ABY, 4 }, {"ORA", 0x1D, &ORA, &ABX, 4 }
		},
		{
			{"AND", 0x21, &AND, &IDX, 6 }, {"AND", 0x25, &AND, &ZPG, 3 }, {"AND", 0x29, &AND, &IMM, 2 }, {"AND", 0x2D, &AND, &ABS, 4 }, {"AND", 0x31, &AND, &IDY, 5 }, {"AND", 0x35, &AND, &ZPX, 4 }, {"AND", 0x39, &AND, &ABY, 4 }, {"AND", 0x3D, &AND, &ABX, 4 }
		},
		{
			{"EOR", 0x41, &EOR, &IDX, 6 }, {"EOR", 0x45, &EOR, &ZPG, 3 }, {"EOR", 0x49, &EOR, &IMM, 2 }, {"EOR", 0x4D, &EOR, &ABS, 4 }, {"EOR", 0x51, &EOR, &IDY, 5 }, {"EOR", 0x55, &EOR, &ZPX, 4 }, {"EOR", 0x59, &EOR, &ABY, 4 }, {"EOR", 0x5D, &EOR, &ABX, 4 }
		},
		{
			{"ADC", 0x61, &ADC, &IDX, 6 }, {"ADC", 0x65, &ADC, &ZPG, 3 }, {"ADC", 0x69, &ADC, &IMM, 2 }, {"ADC", 0x6D, &ADC, &ABS, 4 }, {"ADC", 0x71, &ADC, &IDY, 5 }, {"ADC", 0x75, &ADC, &ZPX, 4 }, {"ADC", 0x79, &ADC, &ABY, 4 }, {"ADC", 0x7D, &ADC, &ABX, 4 }
		},
		{
			{"STA", 0x81, &STA, &IDX, 6 }, {"STA", 0x85, &STA, &ZPG, 3 }, {"???", 0x89, &XXX, &IMP, 2 }, {"STA", 0x8D, &STA, &ABS, 4 }, {"STA", 0x91, &STA, &IDY, 6 }, {"STA", 0x95, &STA, &ZPX, 4 }, {"STA", 0x99, &STA, &ABY, 5 }, {"STA", 0x9D, &STA, &ABX, 5 }
		},
		{
			{"LDA", 0xA1, &LDA, &IDX, 6 }, {"LDA", 0xA5, &LDA, &ZPG, 3 }, {"LDA", 0xA9, &LDA, &IMM, 2 }, {"LDA", 0xAD, &LDA, &ABS, 4 }, {"LDA", 0xB1, &LDA, &IDY, 5 }, {"LDA", 0xB5, &LDA, &ZPX, 4 }, {"LDA", 0xB9, &LDA, &ABY, 4 }, {"LDA", 0xBD, &LDA, &ABX, 4 }
		},
		{
			{"CMP", 0xC1, &CMP, &IDX, 6 }, {"CMP", 0xC5, &CMP, &ZPG, 3 }, {"CMP", 0xC9, &CMP, &IMM, 2 }, {"CMP", 0xCD, &CMP, &ABS, 4 }, {"CMP", 0xD1, &CMP, &IDY, 5 }, {"CMP", 0xD5, &CMP, &ZPX, 4 }, {"CMP", 0xD9, &CMP, &ABY, 4 }, {"CMP", 0xDD, &CMP, &ABX, 4 }
		},
		{
			{"SBC", 0xE1, &SBC, &IDX, 6 }, {"SBC", 0xE5, &SBC, &ZPG, 3 }, {"SBC", 0xE9, &SBC, &IMM, 2 }, {"SBC", 0xED, &SBC, &ABS, 4 }, {"SBC", 0xF1, &SBC, &IDY, 5 }, {"SBC", 0xF5, &SBC, &ZPX, 4 }, {"SBC", 0xF9, &SBC, &ABY, 4 }, {"SBC", 0xFD, &SBC, &ABX, 4 }
		},
	},
	{
		{
			{"???", 0x02, &XXX, &IMP, 2 }, {"ASL", 0x06, &ASL, &ZPG, 2 }, {"ASL", 0x0A, &ASL, &ACC, 2 }, {"ASL", 0x0E, &ASL, &ABS, 6 }, {"???", 0x12, &XXX, &IMP, 2 }, {"ASL", 0x16, &ASL, &ZPX, 6 }, {"???", 0x1A, &XXX, &IMP, 2 }, {"ASL", 0x1E, &ASL, &ABX, 7 }
		},
		{
			{"???", 0x22, &XXX, &IMP, 2 }, {"ROL", 0x26, &ROL, &ZPG, 2 }, {"ROL", 0x2A, &ROL, &ACC, 2 }, {"ROL", 0x2E, &ROL, &ABS, 6 }, {"???", 0x32, &XXX, &IMP, 2 }, {"ROL", 0x36, &ROL, &ZPX, 6 }, {"???", 0x3A, &XXX, &IMP, 2 }, {"ROL", 0x3E, &ROL, &ABX, 7 }
		},
		{
			{"???", 0x42, &XXX, &IMP, 2 }, {"LSR", 0x46, &LSR, &ZPG, 5 }, {"LSR", 0x4A, &LSR, &ACC, 2 }, {"LSR", 0x4E, &LSR, &ABS, 6 }, {"???", 0x52, &XXX, &IMP, 2 }, {"LSR", 0x56, &LSR, &ZPX, 6 }, {"???", 0x5A, &XXX, &IMP, 2 }, {"LSR", 0x5E, &LSR, &ABX, 7 }
		},
		{
			{"???", 0x62, &XXX, &IMP, 2 }, {"ROR", 0x66, &ROR, &ZPG, 2 }, {"ROR", 0x6A, &ROR, &ACC, 2 }, {"ROR", 0x6E, &ROR, &ABS, 6 }, {"???", 0x72, &XXX, &IMP, 2 }, {"ROR", 0x76, &ROR, &ZPX, 6 }, {"???", 0x7A, &XXX, &IMP, 2 }, {"ROR", 0x7E, &ROR, &ABX, 7 }
		},
		{
			{"???", 0x82, &XXX, &IMP, 2 }, {"STX", 0x86, &STX, &ZPG, 3 }, {"TXA", 0x8A, &TXA, &IMP, 2 }, {"STX", 0x8E, &STX, &ABS, 4 }, {"???", 0x92, &XXX, &IMP, 2 }, {"STX", 0x96, &STX, &ZPY, 4 }, {"TSX", 0x9A, &TSX, &IMP, 2 }, {"???", 0x9E, &XXX, &IMP, 2 }
		},
		{
			{"LDX", 0xA2, &LDX, &IMM, 2 }, {"LDX", 0xA6, &LDX, &ZPG, 3 }, {"TAX", 0xAA, &TAX, &IMP, 2 }, {"LDX", 0xAE, &LDX, &ABS, 4 }, {"???", 0xB2, &XXX, &IMP, 2 }, {"LDX", 0xB6, &LDX, &ZPY, 4 }, {"TSX", 0xBA, &TSX, &IMP, 2 }, {"LDX", 0xBE, &LDX, &ABY, 4 }
		},
		{
			{"???", 0xC2, &XXX, &IMP, 2 }, {"DEC", 0xC6, &DEC, &ZPG, 5 }, {"DEX", 0xCA, &DEX, &IMP, 2 }, {"DEC", 0xCE, &DEC, &ABS, 6 }, {"???", 0xD2, &XXX, &IMP, 2 }, {"DEC", 0xD6, &DEC, &ZPX, 6 }, {"???", 0xDA, &XXX, &IMP, 2 }, {"DEC", 0xDE, &DEC, &ABX, 7 }
		},
		{
			{"???", 0xE2, &XXX, &IMP, 2 }, {"INC", 0xE6, &INC, &ZPG, 5 }, {"NOP", 0xEA, &NOP, &IMP, 2 }, {"INC", 0xEE, &INC, &ABS, 6 }, {"???", 0xF2, &XXX, &IMP, 2 }, {"INC", 0xF6, &INC, &ZPX, 6 }, {"???", 0xFA, &XXX, &IMP, 2 }, {"INC", 0xFE, &INC, &ABX, 7 }
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

static uint8_t IDX( )
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

static uint8_t IDY( )
{
	uint16_t ind_addr;

	ind_addr = cpu.read( cpu.PC++ );

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;
	
	ind_addr = cpu.operand_addr + cpu.Y;

	cpu.operand_addr = cpu.read( ind_addr++ );
	cpu.operand_addr |= cpu.read( ind_addr ) << 8;

	if ( ( cpu.operand_addr & 0xff00 ) != ( ind_addr & 0xff00 ) )
		return 1;

	return 0;
}

	// if ( ( cpu.curr_insn->addr_mode != IMP ) && 
	// 	 ( cpu.curr_insn->addr_mode != ACC ) )
	// 	cpu.operand = cpu.read( cpu.operand_addr );

	// if ( cpu.curr_insn->addr_mode == ACC )
	// 	cpu.operand = cpu.A;

//     A + M + C -> A, C                N Z C I D V
//                                      + + + - - +
static uint8_t 
ADC( )
{
	uint16_t tmp;

	cpu.operand = cpu.read( cpu.operand_addr );

	tmp = ( uint16_t )cpu.A + ( uint16_t )cpu.operand + ( uint16_t )GET_FLAG( C );
	SET_FLAG( C, ( tmp > 0xFF ) );

	// 1 + -1 = 0, c <- 1
	if ( ( ( cpu.A & 0x80 )  ^ ( cpu.operand & 0x80 ) ) && !tmp )
		SET_FLAG( C, 1 );

	// Set flags
	SET_FLAG( N, ( tmp & 0x80 ) );
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
	cpu.operand = cpu.read( cpu.operand_addr );

	cpu.A = cpu.A & cpu.operand;

	// Set flags
	SET_FLAG( N, ( cpu.A & 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

//     C <- [76543210] <- 0             N Z C I D V
//                                      + + + - - -
static uint8_t 
ASL( )
{
	uint8_t tmp;

	if ( ( cpu.curr_insn->addr_mode != IMP ) && 
		 ( cpu.curr_insn->addr_mode != ACC ) )
		cpu.operand = cpu.read( cpu.operand_addr );

	if ( cpu.curr_insn->addr_mode == ACC )
		cpu.operand = cpu.A;

	SET_FLAG( C, ( cpu.operand & 0x80 ) >> 7 );

	tmp = cpu.operand << 1;

	if ( cpu.curr_insn->addr_mode != ACC )
		cpu.A = tmp;
	else
		cpu.write( cpu.operand_addr, ( tmp ) );

	// Set flags
	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );

	return 0;
}

// branch on C = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BCC( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( !GET_FLAG( C ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// branch on C = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BCS( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( GET_FLAG( C ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// branch on Z = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BEQ( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( !GET_FLAG( Z ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// bits 7 and 6 of operand are transfered to bit 7 and 6 of SR (N,V);
// the zeroflag is set to the result of operand AND accumulator.
// A AND M, M7 -> N, M6 -> V        N Z C I D V
//                                 M7 + - - - M6
static uint8_t 
BIT( )
{
	cpu.operand = cpu.read( cpu.operand_addr );

	SET_FLAG( N, ( cpu.operand & 0x80 ) );
	SET_FLAG( V, ( cpu.operand & 0x40 ) );
	SET_FLAG( Z, cpu.A & cpu.operand );

	return 0;
}

// branch on N = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BMI( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( GET_FLAG( N ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// branch on Z = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BNE( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( !GET_FLAG( Z ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// branch on N = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BPL( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( !GET_FLAG( N ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// interrupt,                       N Z C I D V
// push PC+2, push SR               - - - 1 - -
static uint8_t 
BRK( )
{
	printf( "Interrupts not implemented\n" );
	exit(1);
	// TODO: What else?
	cpu.write( cpu.SP, ( cpu.PC >> 8 ) & 0x00FF );
	cpu.SP--;
	cpu.write( cpu.SP, cpu.PC & 0x00ff );
	cpu.SP--;

	SET_FLAG( I , 1 );
	return 0;
}

// branch on V = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BVC( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( !GET_FLAG( V ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// branch on V = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t 
BVS( )
{
	uint8_t cycles = 0;
	uint16_t old_pc = cpu.PC;

	if ( GET_FLAG( V ) )
	{
		// One extra cycle if the branch is taken
		cycles++;

		cpu.PC += cpu.operand_addr;

		// One extra cycle if the branch crosses a page
		if ( ( cpu.PC & 0xff00 ) != ( old_pc & 0xff00 ) )
			cpu.cycles++;

	}
	return cycles;
}

// 0 -> C                           N Z C I D V
//                                  - - 0 - - -
static uint8_t 
CLC( )
{
	SET_FLAG( C, 0 );
	return 0;
}

// 0 -> D                           N Z C I D V
//                                  - - - - 0 -
static uint8_t 
CLD( )
{
	SET_FLAG( D, 0 );
	return 0;
}

// 0 -> I                           N Z C I D V
//                                  - - - 0 - -
static uint8_t 
CLI( )
{
	SET_FLAG( I, 0 );
	return 0;
}

// 0 -> V                           N Z C I D V
//                                  - - - - - 0
static uint8_t 
CLV( )
{
	SET_FLAG( V, 0 );
	return 0;
}

// A - M                            N Z C I D V
//                                  + + + - - -
static uint8_t 
CMP( )
{
	uint16_t tmp;

	cpu.operand = cpu.read( cpu.operand_addr );

	tmp = ( uint16_t )cpu.A - ( uint16_t )cpu.operand;
	SET_FLAG( C, ( cpu.A >= cpu.operand ) ? 1 : 0  );

	// Set flags
	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !( tmp & 0xff ) ) );
	
	cpu.A = tmp & 0x00FF;

	return 0;
}

// X - M                            N Z C I D V
//                                  + + + - - -
static uint8_t 
CPX( )
{
	uint8_t tmp;

	cpu.operand = cpu.read( cpu.operand_addr );

	tmp = cpu.X - cpu.operand;
	
	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( C, ( cpu.X >= cpu.operand ) );

	return 0;
}

// Y - M                            N Z C I D V
//                                  + + + - - -
static uint8_t 
CPY( )
{
	uint8_t tmp;

	cpu.operand = cpu.read( cpu.operand_addr );

	tmp = cpu.Y - cpu.operand;

	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( C, ( cpu.Y >= cpu.operand ) );
	return 0;
}

// M - 1 -> M                       N Z C I D V
//                                  + + - - - -
static uint8_t 
DEC( )
{
	uint8_t tmp;

	cpu.operand = cpu.read( cpu.operand_addr );

	tmp = cpu.operand - 1;

	cpu.write( tmp, cpu.operand_addr );

	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );

	return 0;
}

// X - 1 -> X                       N Z C I D V
//                                  + + - - - -
static uint8_t 
DEX( )
{
	cpu.X--;

	SET_FLAG( N, ( cpu.X & 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

// Y - 1 -> Y                       N Z C I D V
//                                  + + - - - -
static uint8_t 
DEY( )
{
	cpu.Y--;

	SET_FLAG( N, ( cpu.Y & 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

// A EOR M -> A                     N Z C I D V
//                                  + + - - - -
static uint8_t 
EOR( )
{
	cpu.operand = cpu.read( cpu.operand_addr );

	cpu.A = cpu.A ^ cpu.operand;

	SET_FLAG( N, ( cpu.A & 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

// M + 1 -> M                       N Z C I D V
//                                  + + - - - -
static uint8_t 
INC( )
{
	uint8_t tmp;

	cpu.operand = cpu.read( cpu.operand_addr );

	tmp = cpu.operand + 1;

	cpu.write( tmp, cpu.operand_addr );

	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );

	return 0;
}


// X + 1 -> X                       N Z C I D V
//                                  + + - - - -
static uint8_t 
INX( )
{
	cpu.X++;

	SET_FLAG( N, ( cpu.X & 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

// Y + 1 -> Y                       N Z C I D V
//                                  + + - - - -
static uint8_t 
INY( )
{
	cpu.Y++;

	SET_FLAG( N, ( cpu.Y & 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

// (PC+1) -> PCL                    N Z C I D V
// (PC+2) -> PCH                    - - - - - -
static uint8_t 
JMP( )
{
	cpu.PC = cpu.operand_addr;
	return 0;
}

// push (PC+2),                     N Z C I D V
// (PC+1) -> PCL                    - - - - - -
// (PC+2) -> PCH
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

// M -> A                           N Z C I D V
//                                  + + - - - -
static uint8_t 
LDA( )
{
	cpu.operand = cpu.read( cpu.operand_addr );
	cpu.A = cpu.operand;

	SET_FLAG( N, ( cpu.A & 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

// M -> X                           N Z C I D V
//                                  + + - - - -
static uint8_t 
LDX( )
{
	cpu.operand = cpu.read( cpu.operand_addr );

	cpu.X = cpu.operand;

	SET_FLAG( N, ( cpu.X & 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

// M -> Y                           N Z C I D V
//                                  + + - - - -
static uint8_t 
LDY( )
{
	cpu.operand = cpu.read( cpu.operand_addr );

	cpu.Y = cpu.operand;

	SET_FLAG( N, ( cpu.Y & 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

// 0 -> [76543210] -> C             N Z C I D V
//                                  0 + + - - -
static uint8_t 
LSR( )
{
	uint8_t tmp;

	if ( ( cpu.curr_insn->addr_mode != IMP ) && 
		 ( cpu.curr_insn->addr_mode != ACC ) )
		cpu.operand = cpu.read( cpu.operand_addr );

	if ( cpu.curr_insn->addr_mode == ACC )
		cpu.operand = cpu.A;

	SET_FLAG( C, ( cpu.operand & 0x1 )  );
	
	tmp = cpu.operand >> 1;


	if ( cpu.curr_insn->addr_mode != ACC )
		cpu.A = tmp;
	else
		cpu.write( cpu.operand_addr, ( tmp ) );

	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( N, 0 );
	
	return 0;
}

// ---                              N Z C I D V
//                                  - - - - - -
static uint8_t 
NOP( )
{
	return 0;
}

// A OR M -> A                      N Z C I D V
//                                  + + - - - -
static uint8_t 
ORA( )
{
	cpu.operand = cpu.read( cpu.operand_addr );

	cpu.A |= cpu.operand;

	SET_FLAG( N, ( cpu.A & 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );
	return 0;
}

// push A                           N Z C I D V
//                                  - - - - - -
static uint8_t 
PHA( )
{
	cpu.write( cpu.SP, cpu.A );
	cpu.SP--;
	return 0;
}

// push SR                          N Z C I D V
//                                  - - - - - -
static uint8_t 
PHP( )
{
	cpu.write( cpu.SP, cpu.flags.reg );
	cpu.SP--;
	return 0;
}

// pull A                           N Z C I D V
//                                  + + - - - -
static uint8_t 
PLA( )
{
	cpu.SP++;
	cpu.A = cpu.read( cpu.SP );

	SET_FLAG( N, ( cpu.A & 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

// pull SR                          N Z C I D V
//                                  from stack
static uint8_t 
PLP( )
{
	cpu.SP++;
	cpu.flags.reg = cpu.read( cpu.SP );
	return 0;
}

// C <- [76543210] <- C             N Z C I D V
//                                  + + + - - -
static uint8_t 
ROL( )
{

	uint8_t tmp;
	uint8_t old_carry = GET_FLAG( C );

	if ( ( cpu.curr_insn->addr_mode != IMP ) && 
		 ( cpu.curr_insn->addr_mode != ACC ) )
		cpu.operand = cpu.read( cpu.operand_addr );

	if ( cpu.curr_insn->addr_mode == ACC )
		cpu.operand = cpu.A;

	SET_FLAG( C, ( cpu.operand & 0x80 ) >> 7 );

	tmp = cpu.operand << 1 | old_carry;

	if ( cpu.curr_insn->addr_mode != ACC )
		cpu.A = tmp;
	else
		cpu.write( cpu.operand_addr, ( tmp ) );

	// Set flags
	SET_FLAG( N, ( tmp & 0x80 ) );
	SET_FLAG( Z, ( !tmp ) );
	
	return 0;
}

// C -> [76543210] -> C             N Z C I D V
//                                  + + + - - -
static uint8_t 
ROR( )
{
	uint8_t tmp;
	uint8_t old_carry = GET_FLAG( C );

	if ( ( cpu.curr_insn->addr_mode != IMP ) && 
		 ( cpu.curr_insn->addr_mode != ACC ) )
		cpu.operand = cpu.read( cpu.operand_addr );

	if ( cpu.curr_insn->addr_mode == ACC )
		cpu.operand = cpu.A;

	SET_FLAG( C, ( cpu.operand & 0x1 )  );
	
	tmp = cpu.operand >> 1 | ( old_carry << 7 );


	if ( cpu.curr_insn->addr_mode != ACC )
		cpu.A = tmp;
	else
		cpu.write( cpu.operand_addr, ( tmp ) );

	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( N, 0 );
	
	return 0;
}

// pull SR, pull PC                 N Z C I D V
//                                  from stack
static uint8_t 
RTI( )
{
	uint16_t tmp;

	cpu.SP++;
	cpu.flags.reg = cpu.read( cpu.SP );

	cpu.SP++;
	tmp = cpu.read( cpu.SP );
	cpu.SP++;
	tmp |=  ( cpu.read( cpu.SP ) << 8 );

	cpu.PC = tmp;

	return 0;
}

// pull PC, PC+1 -> PC              N Z C I D V
//                                  - - - - - -
static uint8_t 
RTS( )
{
	uint16_t tmp;

	cpu.SP++;
	tmp = cpu.read( cpu.SP );
	cpu.SP++;
	tmp |=  ( cpu.read( cpu.SP ) << 8 );

	cpu.PC = tmp;

	return 0;
}

// A - M - C -> A                   N Z C I D V
//                                  + + + - - +
static uint8_t 
SBC( )
{
	uint16_t tmp;
	uint16_t value;

	cpu.operand = cpu.read( cpu.operand_addr );

	value = ( ( uint16_t ) cpu.operand ) ^ 0x00ff;


	tmp = ( uint16_t )cpu.A + value + ( uint16_t )GET_FLAG( C );
	SET_FLAG( C, ( tmp & 0xFF00 ) );

	// Set flags
	SET_FLAG( N, ( tmp & 0x0080 ) );
	SET_FLAG( Z, ( !tmp ) );
	SET_FLAG( V, ( ( tmp ^ ( uint16_t ) cpu.A ) & ( tmp ^ value ) & 0x0080 ) );
	
	cpu.A = tmp & 0x00FF;

	return 0;
}

// 1 -> C                           N Z C I D V
//                                  - - 1 - - -
static uint8_t 
SEC( )
{
	SET_FLAG( C, 1 );
	return 0;
}

// 1 -> D                           N Z C I D V
//                                  - - - - 1 -
static uint8_t 
SED( )
{
	SET_FLAG( D, 1 );
	return 0;
}

// 1 -> I                           N Z C I D V
//                                  - - - 1 - -
static uint8_t 
SEI( )
{
	SET_FLAG( I, 1 );
	return 0;
}

// A -> M                           N Z C I D V
//                                  - - - - - -
static uint8_t 
STA( )
{
	cpu.write( cpu.operand_addr, cpu.A );
	return 0;
}

// X -> M                           N Z C I D V
//                                  - - - - - -
static uint8_t 
STX( )
{
	cpu.write( cpu.operand_addr, cpu.X );
	return 0;
}

// Y -> M                           N Z C I D V
//                                  - - - - - -
static uint8_t 
STY( )
{
	cpu.write( cpu.operand_addr, cpu.Y );
	return 0;
}

// A -> X                           N Z C I D V
//                                  + + - - - -
static uint8_t 
TAX( )
{
	cpu.X = cpu.A;
	SET_FLAG( N, ( cpu.X & 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

// A -> Y                           N Z C I D V
//                                  + + - - - -
static uint8_t 
TAY( )
{
	cpu.Y = cpu.A;
	SET_FLAG( N, ( cpu.Y & 0x80 ) );
	SET_FLAG( Z, ( !cpu.Y ) );

	return 0;
}

// SP -> X                          N Z C I D V
//                                  + + - - - -
static uint8_t 
TSX( )
{
	cpu.X = (uint8_t) cpu.SP;
	SET_FLAG( N, ( cpu.X & 0x80 ) );
	SET_FLAG( Z, ( !cpu.X ) );

	return 0;
}

// X -> A                           N Z C I D V
//                                  + + - - - -
static uint8_t 
TXA( )
{
	cpu.A = cpu.X;
	SET_FLAG( N, ( cpu.A & 0x80 ) );
	SET_FLAG( Z, ( !cpu.A ) );

	return 0;
}

// X -> SP                          N Z C I D V
//                                  - - - - - -
static uint8_t 
TXS( )
{
	cpu.SP = ( (uint16_t) cpu.X ) & 0x00ff;

	return 0;
}

// Y -> A                           N Z C I D V
//                                  + + - - - -
static uint8_t 
TYA( )
{
	cpu.A = cpu.Y;
	SET_FLAG( N, ( cpu.A & 0x80 ) );
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
	printf("N V U B D I Z C\n");
	printf("%d %d %d %d %d %d %d %d\n", 
		GET_FLAG( N ),
		GET_FLAG( V ),
		GET_FLAG( U ),
		GET_FLAG( B ),
		GET_FLAG( D ),
		GET_FLAG( I ),
		GET_FLAG( Z ),
		GET_FLAG( C ));
}

//https://wiki.nesdev.com/w/index.php/CPU_interrupts#IRQ_and_NMI_tick-by-tick_execution
static void
nmi( void )
{
	uint16_t vector = 0xFFFA;
	uint16_t addr;

	// Push PC
	cpu.write( cpu.SP, ( cpu.PC >> 8 ) & 0x00FF );
	cpu.SP--;
	cpu.write( cpu.SP, ( cpu.PC & 0x00FF ) );
	cpu.SP--;

	// Clear B, set I
	SET_FLAG( B, 0 );
	// Push SR
	cpu.write( cpu.SP, cpu.flags.reg );
	cpu.SP--;
	SET_FLAG( I, 1 );


	// Jmp to NMI vector
	addr = cpu.read( vector );
	addr |=  ( cpu.read( vector + 1 ) << 8 );

	cpu.PC = addr;
}

static void
irq( void )
{
	uint16_t vector = 0xFFFE;
	uint16_t addr;

	if ( GET_FLAG( I ) )
	{
		// Push PC
		cpu.write( cpu.SP, ( cpu.PC >> 8 ) & 0x00FF );
		cpu.SP--;
		cpu.write( cpu.SP, ( cpu.PC & 0x00FF ) );
		cpu.SP--;

		// Clear B, set I
		SET_FLAG( B, 0 );
		// Push SR
		cpu.write( cpu.SP, cpu.flags.reg );
		cpu.SP--;
		SET_FLAG( I, 1 );


		// Jmp to NMI vector
		addr = cpu.read( vector );
		addr |=  ( cpu.read( vector + 1 ) << 8 );

		cpu.PC = addr;
	}
}

// TODO: read the docs for the 6502 on what a reset state looks like
static void
reset( void )
{
	SET_FLAG( U, 1 );
	cpu.SP = (uint16_t)0xfd;
	cpu.PC = cpu.read( 0xFFFC ) | cpu.read( 0xFFFD ) << 8;

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

	// Set the initial cycle count
	cpu.cycles = cpu.curr_insn->cycles;

	// Calling addr_mode will resolve operand and any addresses
	// as well as determine any additional cycles to be added on
	// for memory access types. Branc instructions can incur
	// additional cycles but need to be resolved at execution
	cpu.curr_insn->addr_mode();

	return cpu.opcode;
}

static uint8_t
execute( )
{
	cpu.cycles += cpu.curr_insn->execute( );
	return 0;
}

static void
clock( )
{
	uint8_t buf[0x100];
	
	if ( cpu.cycles == 0 )
	{
		cpu.fetch( );

		printf("%04x: %02x %s %04x / %02x\n",
				cpu.start_pc,
				cpu.opcode,
				cpu.curr_insn->mnem,
				cpu.operand_addr,
				cpu.operand);

		// Execution may add up to 2 cycles if a branch is taken that crosses
		// a page boundry.
		cpu.execute( );

		cpu.print_regs( );
		cpu.bus->debug_read(0x200-0x10, buf, 0x20);
		printf("Stack:\n");
		hex_dump( buf, 0x20 );
		cpu.bus->debug_read(cpu.PC, buf, 0x10);
		printf("%04x: \n",cpu.PC);
		hex_dump( buf, 0x10 );
		cpu.bus->debug_read(0, buf, 0x20);
		printf("%04x: \n",0);
		hex_dump( buf, 0x20 );
		printf("\n");
	}
	printf("%d cycles for this op\n", cpu.cycles);
	cpu.cycles--;
}

static void
connect_bus( void *bus )
{
	cpu.bus = ( struct nesbus * ) bus;
}

struct cpu6502 *
cpu6502_init(  )
{
	cpu.nmi 	= nmi;
	cpu.irq 	= irq;
	cpu.reset 	= reset;
	cpu.read 	= read;
	cpu.write 	= write;
	cpu.fetch 	= fetch;
	cpu.execute	= execute;
	cpu.clock 	= clock;
	cpu.connect_bus = connect_bus;
	cpu.print_regs = print_regs;

	return &cpu;
}
