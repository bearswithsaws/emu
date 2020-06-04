#ifndef __6502_H__
#define __6502_H__

#include <stdint.h>
#include "nesbus.h"

struct cpu6502;

typedef void ( *fp_reset ) ( void );
typedef uint8_t ( *fp_read ) ( uint16_t addr );
typedef void ( *fp_write ) ( uint16_t addr, uint8_t data );
typedef uint8_t ( *fp_fetch ) ( void );
typedef uint8_t ( *fp_decode ) ( void );
typedef uint8_t ( *fp_execute ) ( void );

typedef uint8_t ( *fp_addr_mode )( void );
typedef uint8_t ( *fp_mnem )( void );

struct instruction
{
	char *mnem;
	uint8_t opcode;
	fp_mnem execute;
	fp_addr_mode addr_mode;
	uint8_t cycles;
};

struct cpu6502
{
	fp_reset reset;
	fp_read read;
	fp_write write;
	fp_fetch fetch;
	fp_decode decode;
	fp_execute execute;

	struct nesbus *bus;

	uint16_t pc;
	union
	{
		struct
		{
			uint8_t n: 1 ;
			uint8_t v: 1 ;
			uint8_t u: 1 ;
			uint8_t b: 1 ;
			uint8_t d: 1 ;
			uint8_t i: 1 ;
			uint8_t z: 1 ;
			uint8_t c: 1 ;
		};
		uint8_t reg;
	} flags;
	uint16_t sp;
	uint8_t a;
	uint8_t y;
	uint8_t x;
	uint8_t op;

	// DEBUG purposes
	uint16_t start_pc;

	struct instruction *curr_insn;
	uint8_t operand;
	uint16_t operand_addr;

};

// Each should return how many extra clock cycles are required
// based on the caveats in the cpu docs
uint8_t IMP( );
uint8_t ACC( );
uint8_t IMM( );
uint8_t ZPG( );
uint8_t ZPX( );
uint8_t ZPY( );
uint8_t REL( );
uint8_t ABS( );
uint8_t ABX( );
uint8_t ABY( );
uint8_t IND( );
uint8_t IZX( );
uint8_t IZY( );


uint8_t ADC( );
uint8_t AND( );
uint8_t ASL( );
uint8_t BCC( );
uint8_t BCS( );
uint8_t BEQ( );
uint8_t BIT( );
uint8_t BMI( );
uint8_t BNE( );
uint8_t BPL( );
uint8_t BRK( );
uint8_t BVC( );
uint8_t BVS( );
uint8_t CLC( );
uint8_t CLD( );
uint8_t CLI( );
uint8_t CLV( );
uint8_t CMP( );
uint8_t CPX( );
uint8_t CPY( );
uint8_t DEC( );
uint8_t DEX( );
uint8_t DEY( );
uint8_t EOR( );
uint8_t INC( );
uint8_t INX( );
uint8_t INY( );
uint8_t JMP( );
uint8_t JSR( );
uint8_t LDA( );
uint8_t LDX( );
uint8_t LDY( );
uint8_t LSR( );
uint8_t NOP( );
uint8_t ORA( );
uint8_t PHA( );
uint8_t PHP( );
uint8_t PLA( );
uint8_t PLP( );
uint8_t ROL( );
uint8_t ROR( );
uint8_t RTI( );
uint8_t RTS( );
uint8_t SBC( );
uint8_t SEC( );
uint8_t SED( );
uint8_t SEI( );
uint8_t STA( );
uint8_t STX( );
uint8_t STY( );
uint8_t TAX( );
uint8_t TAY( );
uint8_t TSX( );
uint8_t TXA( );
uint8_t TXS( );
uint8_t TYA( );
uint8_t XXX( ); // invalid opcode


#define DECODE_A( inst ) ( ( inst >> 5 ) & 0x7 )
#define DECODE_B( inst ) ( ( inst >> 2 ) & 0x7 )
#define DECODE_C( inst ) ( ( inst ) & 0x3 )

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

struct cpu6502 * cpu6502_init( struct nesbus *bus );

#endif /* __6502_H__ */
