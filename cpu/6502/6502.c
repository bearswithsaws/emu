// 6502.c

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502.h"
#include "bus.h"

#include "debug.h"

static struct cpu6502 cpu = {0};

// Each should return how many extra clock cycles are required
// based on the caveats in the cpu docs
static uint8_t IMP();
static uint8_t ACC();
static uint8_t IMM();
static uint8_t ZPG();
static uint8_t ZPX();
static uint8_t ZPY();
static uint8_t REL();
static uint8_t ABS();
static uint8_t ABX();
static uint8_t ABY();
static uint8_t IND();
static uint8_t IDX();
static uint8_t IDY();

static uint8_t ADC();
static uint8_t AND();
static uint8_t ASL();
static uint8_t BCC();
static uint8_t BCS();
static uint8_t BEQ();
static uint8_t BIT();
static uint8_t BMI();
static uint8_t BNE();
static uint8_t BPL();
static uint8_t BRK();
static uint8_t BVC();
static uint8_t BVS();
static uint8_t CLC();
static uint8_t CLD();
static uint8_t CLI();
static uint8_t CLV();
static uint8_t CMP();
static uint8_t CPX();
static uint8_t CPY();
static uint8_t DEC();
static uint8_t DEX();
static uint8_t DEY();
static uint8_t EOR();
static uint8_t INC();
static uint8_t INX();
static uint8_t INY();
static uint8_t JMP();
static uint8_t JSR();
static uint8_t LDA();
static uint8_t LDX();
static uint8_t LDY();
static uint8_t LSR();
static uint8_t NOP();
static uint8_t ORA();
static uint8_t PHA();
static uint8_t PHP();
static uint8_t PLA();
static uint8_t PLP();
static uint8_t ROL();
static uint8_t ROR();
static uint8_t RTI();
static uint8_t RTS();
static uint8_t SBC();
static uint8_t SEC();
static uint8_t SED();
static uint8_t SEI();
static uint8_t STA();
static uint8_t STX();
static uint8_t STY();
static uint8_t TAX();
static uint8_t TAY();
static uint8_t TSX();
static uint8_t TXA();
static uint8_t TXS();
static uint8_t TYA();
static uint8_t XXX(); // invalid opcode

/* Illegal (undocumented) opcode declarations */
static uint8_t SLO(); /* cc=3 a=0: ASL mem, then ORA A */
static uint8_t RLA(); /* cc=3 a=1: ROL mem, then AND A */
static uint8_t SRE(); /* cc=3 a=2: LSR mem, then EOR A */
static uint8_t RRA(); /* cc=3 a=3: ROR mem, then ADC A */
static uint8_t SAX(); /* cc=3 a=4: store A & X to memory */
static uint8_t LAX(); /* cc=3 a=5: load A and X from memory */
static uint8_t DCP(); /* cc=3 a=6: DEC mem, then CMP */
static uint8_t ISB(); /* cc=3 a=7: INC mem, then SBC */
/* Special b=2 (immediate) variants */
static uint8_t ANC(); /* $0B/$2B: AND imm, C = N */
static uint8_t ALR(); /* $4B: AND imm, then LSR A */
static uint8_t ARR(); /* $6B: AND imm, then ROR A (special flags) */
static uint8_t XAA(); /* $8B: A = X & imm (unstable) */
static uint8_t SBX(); /* $CB: X = (A & X) - imm */
static uint8_t USB(); /* $EB: same as SBC #imm */
/* Unstable address-high opcodes (a=4 row) */
static uint8_t AHX(); /* $93/$9F: store A & X & (addr_H+1) */
static uint8_t TAS(); /* $9B: S = A&X; store S & (addr_H+1) */
static uint8_t LAS(); /* $BB: A = X = S = S & mem[addr] */


// This lookup table is based on the layout as described in:
// https://www.masswerk.at/6502/6502_instruction_set.html
// c a b
static struct instruction instruction_table[4][8][8] = {
    {
        {{"BRK", 0x00, &BRK, &IMP, 7},
         {"NOP", 0x04, &XXX, &ZPG, 3},
         {"PHP", 0x08, &PHP, &IMP, 3},
         {"NOP", 0x0C, &XXX, &ABS, 4},
         {"BPL", 0x10, &BPL, &REL, 2},
         {"NOP", 0x14, &XXX, &ZPX, 4},
         {"CLC", 0x18, &CLC, &IMP, 2},
         {"NOP", 0x1C, &XXX, &ABX, 4}},
        {{"JSR", 0x20, &JSR, &ABS, 6},
         {"BIT", 0x24, &BIT, &ZPG, 3},
         {"PLP", 0x28, &PLP, &IMP, 4},
         {"BIT", 0x2C, &BIT, &ABS, 4},
         {"BMI", 0x30, &BMI, &REL, 2},
         {"NOP", 0x34, &XXX, &ZPX, 4},
         {"SEC", 0x38, &SEC, &IMP, 2},
         {"NOP", 0x3C, &XXX, &ABX, 4}},
        {{"RTI", 0x40, &RTI, &IMP, 6},
         {"NOP", 0x44, &XXX, &ZPG, 3},
         {"PHA", 0x48, &PHA, &IMP, 3},
         {"JMP", 0x4C, &JMP, &ABS, 3},
         {"BVC", 0x50, &BVC, &REL, 2},
         {"NOP", 0x54, &XXX, &ZPX, 4},
         {"CLI", 0x58, &CLI, &IMP, 2},
         {"NOP", 0x5C, &XXX, &ABX, 4}},
        {{"RTS", 0x60, &RTS, &IMP, 6},
         {"NOP", 0x64, &XXX, &ZPG, 3},
         {"PLA", 0x68, &PLA, &IMP, 4},
         {"JMP", 0x6C, &JMP, &IND, 5},
         {"BVS", 0x70, &BVS, &REL, 2},
         {"NOP", 0x74, &XXX, &ZPX, 4},
         {"SEI", 0x78, &SEI, &IMP, 2},
         {"NOP", 0x7C, &XXX, &ABX, 4}},
        {{"NOP", 0x80, &XXX, &IMM, 2},
         {"STY", 0x84, &STY, &ZPG, 3},
         {"DEY", 0x88, &DEY, &IMP, 2},
         {"STY", 0x8C, &STY, &ABS, 4},
         {"BCC", 0x90, &BCC, &REL, 2},
         {"STY", 0x94, &STY, &ZPX, 4},
         {"TYA", 0x98, &TYA, &IMP, 2},
         {"NOP", 0x9C, &XXX, &ABX, 4}},
        {{"LDY", 0xA0, &LDY, &IMM, 2},
         {"LDY", 0xA4, &LDY, &ZPG, 3},
         {"TAY", 0xA8, &TAY, &IMP, 2},
         {"LDY", 0xAC, &LDY, &ABS, 4},
         {"BCS", 0xB0, &BCS, &REL, 2},
         {"LDY", 0xB4, &LDY, &ZPX, 4},
         {"CLV", 0xB8, &CLV, &IMP, 2},
         {"LDY", 0xBC, &LDY, &ABX, 4}},
        {{"CPY", 0xC0, &CPY, &IMM, 2},
         {"CPY", 0xC4, &CPY, &ZPG, 3},
         {"INY", 0xC8, &INY, &IMP, 2},
         {"CPY", 0xCC, &CPY, &ABS, 4},
         {"BNE", 0xD0, &BNE, &REL, 2},
         {"NOP", 0xD4, &XXX, &ZPX, 4},
         {"CLD", 0xD8, &CLD, &IMP, 2},
         {"NOP", 0xDC, &XXX, &ABX, 4}},
        {{"CPX", 0xE0, &CPX, &IMM, 2},
         {"CPX", 0xE4, &CPX, &ZPG, 3},
         {"INX", 0xE8, &INX, &IMP, 2},
         {"CPX", 0xEC, &CPX, &ABS, 4},
         {"BEQ", 0xF0, &BEQ, &REL, 2},
         {"NOP", 0xF4, &XXX, &ZPX, 4},
         {"SED", 0xF8, &SED, &IMP, 2},
         {"NOP", 0xFC, &XXX, &ABX, 4}},
    },
    {
        {{"ORA", 0x01, &ORA, &IDX, 6},
         {"ORA", 0x05, &ORA, &ZPG, 3},
         {"ORA", 0x09, &ORA, &IMM, 2},
         {"ORA", 0x0D, &ORA, &ABS, 4},
         {"ORA", 0x11, &ORA, &IDY, 5},
         {"ORA", 0x15, &ORA, &ZPX, 2},
         {"ORA", 0x19, &ORA, &ABY, 4},
         {"ORA", 0x1D, &ORA, &ABX, 4}},
        {{"AND", 0x21, &AND, &IDX, 6},
         {"AND", 0x25, &AND, &ZPG, 3},
         {"AND", 0x29, &AND, &IMM, 2},
         {"AND", 0x2D, &AND, &ABS, 4},
         {"AND", 0x31, &AND, &IDY, 5},
         {"AND", 0x35, &AND, &ZPX, 4},
         {"AND", 0x39, &AND, &ABY, 4},
         {"AND", 0x3D, &AND, &ABX, 4}},
        {{"EOR", 0x41, &EOR, &IDX, 6},
         {"EOR", 0x45, &EOR, &ZPG, 3},
         {"EOR", 0x49, &EOR, &IMM, 2},
         {"EOR", 0x4D, &EOR, &ABS, 4},
         {"EOR", 0x51, &EOR, &IDY, 5},
         {"EOR", 0x55, &EOR, &ZPX, 4},
         {"EOR", 0x59, &EOR, &ABY, 4},
         {"EOR", 0x5D, &EOR, &ABX, 4}},
        {{"ADC", 0x61, &ADC, &IDX, 6},
         {"ADC", 0x65, &ADC, &ZPG, 3},
         {"ADC", 0x69, &ADC, &IMM, 2},
         {"ADC", 0x6D, &ADC, &ABS, 4},
         {"ADC", 0x71, &ADC, &IDY, 5},
         {"ADC", 0x75, &ADC, &ZPX, 4},
         {"ADC", 0x79, &ADC, &ABY, 4},
         {"ADC", 0x7D, &ADC, &ABX, 4}},
        {{"STA", 0x81, &STA, &IDX, 6},
         {"STA", 0x85, &STA, &ZPG, 3},
         {"NOP", 0x89, &XXX, &IMM, 2},
         {"STA", 0x8D, &STA, &ABS, 4},
         {"STA", 0x91, &STA, &IDY, 6},
         {"STA", 0x95, &STA, &ZPX, 4},
         {"STA", 0x99, &STA, &ABY, 5},
         {"STA", 0x9D, &STA, &ABX, 5}},
        {{"LDA", 0xA1, &LDA, &IDX, 6},
         {"LDA", 0xA5, &LDA, &ZPG, 3},
         {"LDA", 0xA9, &LDA, &IMM, 2},
         {"LDA", 0xAD, &LDA, &ABS, 4},
         {"LDA", 0xB1, &LDA, &IDY, 5},
         {"LDA", 0xB5, &LDA, &ZPX, 4},
         {"LDA", 0xB9, &LDA, &ABY, 4},
         {"LDA", 0xBD, &LDA, &ABX, 4}},
        {{"CMP", 0xC1, &CMP, &IDX, 6},
         {"CMP", 0xC5, &CMP, &ZPG, 3},
         {"CMP", 0xC9, &CMP, &IMM, 2},
         {"CMP", 0xCD, &CMP, &ABS, 4},
         {"CMP", 0xD1, &CMP, &IDY, 5},
         {"CMP", 0xD5, &CMP, &ZPX, 4},
         {"CMP", 0xD9, &CMP, &ABY, 4},
         {"CMP", 0xDD, &CMP, &ABX, 4}},
        {{"SBC", 0xE1, &SBC, &IDX, 6},
         {"SBC", 0xE5, &SBC, &ZPG, 3},
         {"SBC", 0xE9, &SBC, &IMM, 2},
         {"SBC", 0xED, &SBC, &ABS, 4},
         {"SBC", 0xF1, &SBC, &IDY, 5},
         {"SBC", 0xF5, &SBC, &ZPX, 4},
         {"SBC", 0xF9, &SBC, &ABY, 4},
         {"SBC", 0xFD, &SBC, &ABX, 4}},
    },
    {
        {{"???", 0x02, &XXX, &IMP, 2},
         {"ASL", 0x06, &ASL, &ZPG, 2},
         {"ASL", 0x0A, &ASL, &ACC, 2},
         {"ASL", 0x0E, &ASL, &ABS, 6},
         {"???", 0x12, &XXX, &IMP, 2},
         {"ASL", 0x16, &ASL, &ZPX, 6},
         {"???", 0x1A, &XXX, &IMP, 2},
         {"ASL", 0x1E, &ASL, &ABX, 7}},
        {{"???", 0x22, &XXX, &IMP, 2},
         {"ROL", 0x26, &ROL, &ZPG, 2},
         {"ROL", 0x2A, &ROL, &ACC, 2},
         {"ROL", 0x2E, &ROL, &ABS, 6},
         {"???", 0x32, &XXX, &IMP, 2},
         {"ROL", 0x36, &ROL, &ZPX, 6},
         {"???", 0x3A, &XXX, &IMP, 2},
         {"ROL", 0x3E, &ROL, &ABX, 7}},
        {{"???", 0x42, &XXX, &IMP, 2},
         {"LSR", 0x46, &LSR, &ZPG, 5},
         {"LSR", 0x4A, &LSR, &ACC, 2},
         {"LSR", 0x4E, &LSR, &ABS, 6},
         {"???", 0x52, &XXX, &IMP, 2},
         {"LSR", 0x56, &LSR, &ZPX, 6},
         {"???", 0x5A, &XXX, &IMP, 2},
         {"LSR", 0x5E, &LSR, &ABX, 7}},
        {{"???", 0x62, &XXX, &IMP, 2},
         {"ROR", 0x66, &ROR, &ZPG, 2},
         {"ROR", 0x6A, &ROR, &ACC, 2},
         {"ROR", 0x6E, &ROR, &ABS, 6},
         {"???", 0x72, &XXX, &IMP, 2},
         {"ROR", 0x76, &ROR, &ZPX, 6},
         {"???", 0x7A, &XXX, &IMP, 2},
         {"ROR", 0x7E, &ROR, &ABX, 7}},
        {{"NOP", 0x82, &XXX, &IMM, 2},
         {"STX", 0x86, &STX, &ZPG, 3},
         {"TXA", 0x8A, &TXA, &IMP, 2},
         {"STX", 0x8E, &STX, &ABS, 4},
         {"???", 0x92, &XXX, &IMP, 2},
         {"STX", 0x96, &STX, &ZPY, 4},
         {"TXS", 0x9A, &TXS, &IMP, 2},
         {"???", 0x9E, &XXX, &IMP, 2}},
        {{"LDX", 0xA2, &LDX, &IMM, 2},
         {"LDX", 0xA6, &LDX, &ZPG, 3},
         {"TAX", 0xAA, &TAX, &IMP, 2},
         {"LDX", 0xAE, &LDX, &ABS, 4},
         {"???", 0xB2, &XXX, &IMP, 2},
         {"LDX", 0xB6, &LDX, &ZPY, 4},
         {"TSX", 0xBA, &TSX, &IMP, 2},
         {"LDX", 0xBE, &LDX, &ABY, 4}},
        {{"NOP", 0xC2, &XXX, &IMM, 2},
         {"DEC", 0xC6, &DEC, &ZPG, 5},
         {"DEX", 0xCA, &DEX, &IMP, 2},
         {"DEC", 0xCE, &DEC, &ABS, 6},
         {"???", 0xD2, &XXX, &IMP, 2},
         {"DEC", 0xD6, &DEC, &ZPX, 6},
         {"???", 0xDA, &XXX, &IMP, 2},
         {"DEC", 0xDE, &DEC, &ABX, 7}},
        {{"NOP", 0xE2, &XXX, &IMM, 2},
         {"INC", 0xE6, &INC, &ZPG, 5},
         {"NOP", 0xEA, &NOP, &IMP, 2},
         {"INC", 0xEE, &INC, &ABS, 6},
         {"???", 0xF2, &XXX, &IMP, 2},
         {"INC", 0xF6, &INC, &ZPX, 6},
         {"???", 0xFA, &XXX, &IMP, 2},
         {"INC", 0xFE, &INC, &ABX, 7}},
    },
    /* cc=3: illegal/undocumented opcodes [a][b] */
    {
        /* a=0: SLO (ASL + ORA) */
        {{"SLO", 0x03, &SLO, &IDX, 8},
         {"SLO", 0x07, &SLO, &ZPG, 5},
         {"ANC", 0x0B, &ANC, &IMM, 2},
         {"SLO", 0x0F, &SLO, &ABS, 6},
         {"SLO", 0x13, &SLO, &IDY, 8},
         {"SLO", 0x17, &SLO, &ZPX, 6},
         {"SLO", 0x1B, &SLO, &ABY, 7},
         {"SLO", 0x1F, &SLO, &ABX, 7}},
        /* a=1: RLA (ROL + AND) */
        {{"RLA", 0x23, &RLA, &IDX, 8},
         {"RLA", 0x27, &RLA, &ZPG, 5},
         {"ANC", 0x2B, &ANC, &IMM, 2},
         {"RLA", 0x2F, &RLA, &ABS, 6},
         {"RLA", 0x33, &RLA, &IDY, 8},
         {"RLA", 0x37, &RLA, &ZPX, 6},
         {"RLA", 0x3B, &RLA, &ABY, 7},
         {"RLA", 0x3F, &RLA, &ABX, 7}},
        /* a=2: SRE (LSR + EOR) */
        {{"SRE", 0x43, &SRE, &IDX, 8},
         {"SRE", 0x47, &SRE, &ZPG, 5},
         {"ALR", 0x4B, &ALR, &IMM, 2},
         {"SRE", 0x4F, &SRE, &ABS, 6},
         {"SRE", 0x53, &SRE, &IDY, 8},
         {"SRE", 0x57, &SRE, &ZPX, 6},
         {"SRE", 0x5B, &SRE, &ABY, 7},
         {"SRE", 0x5F, &SRE, &ABX, 7}},
        /* a=3: RRA (ROR + ADC) */
        {{"RRA", 0x63, &RRA, &IDX, 8},
         {"RRA", 0x67, &RRA, &ZPG, 5},
         {"ARR", 0x6B, &ARR, &IMM, 2},
         {"RRA", 0x6F, &RRA, &ABS, 6},
         {"RRA", 0x73, &RRA, &IDY, 8},
         {"RRA", 0x77, &RRA, &ZPX, 6},
         {"RRA", 0x7B, &RRA, &ABY, 7},
         {"RRA", 0x7F, &RRA, &ABX, 7}},
        /* a=4: SAX family (store A & X) */
        {{"SAX", 0x83, &SAX, &IDX, 6},
         {"SAX", 0x87, &SAX, &ZPG, 3},
         {"XAA", 0x8B, &XAA, &IMM, 2},
         {"SAX", 0x8F, &SAX, &ABS, 4},
         {"AHX", 0x93, &AHX, &IDY, 6},
         {"SAX", 0x97, &SAX, &ZPY, 4},
         {"TAS", 0x9B, &TAS, &ABY, 5},
         {"AHX", 0x9F, &AHX, &ABY, 5}},
        /* a=5: LAX family (load A and X) */
        {{"LAX", 0xA3, &LAX, &IDX, 6},
         {"LAX", 0xA7, &LAX, &ZPG, 3},
         {"LAX", 0xAB, &LAX, &IMM, 2},
         {"LAX", 0xAF, &LAX, &ABS, 4},
         {"LAX", 0xB3, &LAX, &IDY, 5},
         {"LAX", 0xB7, &LAX, &ZPY, 4},
         {"LAS", 0xBB, &LAS, &ABY, 4},
         {"LAX", 0xBF, &LAX, &ABY, 4}},
        /* a=6: DCP (DEC + CMP) */
        {{"DCP", 0xC3, &DCP, &IDX, 8},
         {"DCP", 0xC7, &DCP, &ZPG, 5},
         {"SBX", 0xCB, &SBX, &IMM, 2},
         {"DCP", 0xCF, &DCP, &ABS, 6},
         {"DCP", 0xD3, &DCP, &IDY, 8},
         {"DCP", 0xD7, &DCP, &ZPX, 6},
         {"DCP", 0xDB, &DCP, &ABY, 7},
         {"DCP", 0xDF, &DCP, &ABX, 7}},
        /* a=7: ISB (INC + SBC) */
        {{"ISB", 0xE3, &ISB, &IDX, 8},
         {"ISB", 0xE7, &ISB, &ZPG, 5},
         {"USB", 0xEB, &USB, &IMM, 2},
         {"ISB", 0xEF, &ISB, &ABS, 6},
         {"ISB", 0xF3, &ISB, &IDY, 8},
         {"ISB", 0xF7, &ISB, &ZPX, 6},
         {"ISB", 0xFB, &ISB, &ABY, 7},
         {"ISB", 0xFF, &ISB, &ABX, 7}},
    },
};

static uint8_t IMP() {
    // No operand
    cpu.operand = 0;
    return 0;
}

static uint8_t ACC() {
    // Operand is implied to be the A register
    cpu.operand = cpu.A;
    return 0;
}

static uint8_t IMM() {
    cpu.operand_addr = cpu.PC++;

    return 0;
}

static uint8_t ZPG() {
    cpu.operand_addr = (uint16_t)cpu.read(cpu.PC++);

    return 0;
}

static uint8_t ZPX() {
    cpu.operand_addr = ((uint16_t)cpu.read(cpu.PC++) + cpu.X) & 0xff;
    log_print("ZPX OPERAND ADDR: %02x\n", cpu.operand_addr);
    return 0;
}

static uint8_t ZPY() {
    cpu.operand_addr = ((uint16_t)cpu.read(cpu.PC++) + cpu.Y) & 0xff;

    return 0;
}

static uint8_t REL() {
    uint16_t rel_addr;

    rel_addr = cpu.read(cpu.PC++);

    if (rel_addr & 0x80)
        rel_addr |= 0xFF00;

    cpu.operand_addr = rel_addr;

    return 1;
}

static uint8_t ABS() {
    cpu.operand_addr = cpu.read(cpu.PC++);
    cpu.operand_addr |= cpu.read(cpu.PC++) << 8;

    return 0;
}

static uint8_t ABX() {
    uint16_t page_check;

    cpu.operand_addr = cpu.read(cpu.PC++);
    page_check = cpu.read(cpu.PC++);
    cpu.operand_addr |= page_check << 8;
    cpu.operand_addr += cpu.X;

    // According to the 6502 manual, if the addition of X causes
    // this to cross a page, then add one cycle
    if ((cpu.operand_addr >> 8) != page_check)
        return 1;

    return 0;
}

static uint8_t ABY() {
    uint16_t page_check;

    cpu.operand_addr = cpu.read(cpu.PC++);
    page_check = cpu.read(cpu.PC++);
    cpu.operand_addr |= page_check << 8;
    cpu.operand_addr += cpu.Y;

    // According to the 6502 manual, if the addition of Y causes
    // this to cross a page, then add one cycle
    if ((cpu.operand_addr >> 8) != page_check)
        return 1;

    return 0;
}

static uint8_t IND() {
    uint16_t ind_addr;

    ind_addr = cpu.read(cpu.PC++);
    ind_addr |= cpu.read(cpu.PC++) << 8;

    if ((ind_addr & 0x00FF) == 0xFF) {
        // https://www.qmtpro.com/~nes/misc/nestest.txt
        // 007h - JMP () data reading didn't wrap properly (this fails on a
        // 65C02)
        cpu.operand_addr = cpu.read(ind_addr);
        cpu.operand_addr |= cpu.read(ind_addr & 0xff00) << 8;
        log_print("IND operand addr %04x (from %04x wrapped)\n",
                  cpu.operand_addr, ind_addr);
    } else {
        cpu.operand_addr = cpu.read(ind_addr++);
        cpu.operand_addr |= cpu.read(ind_addr) << 8;
    }
    return 0;
}

static uint8_t IDX() {
    uint16_t ind_addr;

    ind_addr = cpu.read(cpu.PC++);
    log_print("IDX indirect addr: %04x\n", ind_addr);
    ind_addr += cpu.X;

    // Zero page wrap around
    ind_addr &= 0xff;
    log_print("IDX indirect addr + x & ff: %04x\n", ind_addr);

    cpu.operand_addr = cpu.read(ind_addr++);
    cpu.operand_addr |= cpu.read(ind_addr & 0xff) << 8;
    log_print("IDX OPERAND ADDR: %02x\n", cpu.operand_addr);

    return 0;
}

static uint8_t IDY() {
    uint16_t ind_addr;

    ind_addr = cpu.read(cpu.PC++);
    log_print("IDY indirect addr in zero page: %04x\n", ind_addr);

    cpu.operand_addr = cpu.read(ind_addr++);
    cpu.operand_addr |= cpu.read(ind_addr & 0xFF) << 8;
    log_print("IDY OPERAND ADDR: %02x\n", cpu.operand_addr);

    ind_addr = cpu.operand_addr + cpu.Y;
    log_print("IDY addr + y: %04x\n", ind_addr);

    cpu.operand_addr = ind_addr;

    if ((cpu.operand_addr & 0xff00) != (ind_addr & 0xff00))
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
static uint8_t ADC() {
    uint16_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);

    tmp = (uint16_t)cpu.A + (uint16_t)cpu.operand + (uint16_t)GET_FLAG(C);
    SET_FLAG(C, (tmp > 0xFF));

    // 1 + -1 = 0, c <- 1
    if (((cpu.A & 0x80) ^ (cpu.operand & 0x80)) && !tmp)
        SET_FLAG(C, 1);

    // Set flags
    tmp &= 0x00FF;
    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!tmp));
    // See
    // https://github.com/OneLoneCoder/olcNES/blob/master/Part%232%20-%20CPU/olc6502.cpp#L601
    SET_FLAG(V, (~((uint16_t)cpu.A ^ (uint16_t)cpu.operand) &
                 ((uint16_t)cpu.A ^ (uint16_t)tmp)) &
                    0x0080);

    cpu.A = tmp & 0x00FF;

    return 0;
}

//     A AND M -> A                     N Z C I D V
//                                      + + - - - -
static uint8_t AND() {
    cpu.operand = cpu.read(cpu.operand_addr);

    cpu.A = cpu.A & cpu.operand;

    // Set flags
    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));

    return 0;
}

//     C <- [76543210] <- 0             N Z C I D V
//                                      + + + - - -
static uint8_t ASL() {
    uint8_t tmp;

    if ((cpu.curr_insn->addr_mode != IMP) && (cpu.curr_insn->addr_mode != ACC))
        cpu.operand = cpu.read(cpu.operand_addr);

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.operand = cpu.A;

    SET_FLAG(C, (cpu.operand & 0x80) >> 7);

    tmp = cpu.operand << 1;

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.A = tmp;
    else
        cpu.write(cpu.operand_addr, (tmp));

    // Set flags
    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!tmp));

    return 0;
}

// branch on C = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t BCC() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (!GET_FLAG(C)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// branch on C = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t BCS() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (GET_FLAG(C)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// branch on Z = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t BEQ() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (GET_FLAG(Z)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// bits 7 and 6 of operand are transfered to bit 7 and 6 of SR (N,V);
// the zeroflag is set to the result of operand AND accumulator.
// A AND M, M7 -> N, M6 -> V        N Z C I D V
//                                 M7 + - - - M6
static uint8_t BIT() {
    uint16_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);

    tmp = cpu.A & cpu.operand;
    SET_FLAG(Z, (tmp & 0x00ff) == 0x00);

    SET_FLAG(N, (cpu.operand & 0x80));
    SET_FLAG(V, (cpu.operand & 0x40));

    return 0;
}

// branch on N = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t BMI() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (GET_FLAG(N)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// branch on Z = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t BNE() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (!GET_FLAG(Z)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// branch on N = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t BPL() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (!GET_FLAG(N)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// interrupt,                       N Z C I D V
// push PC+2, push SR               - - - 1 - -
static uint8_t BRK() {
    // PC already points one past the BRK opcode (the ignored padding byte).
    // Push PC+1 so RTI returns to the instruction after the padding byte.
    uint16_t ret = cpu.PC + 1;
    cpu.write(SP(cpu), (ret >> 8) & 0xFF);
    DEC_SP(cpu);
    cpu.write(SP(cpu), ret & 0xFF);
    DEC_SP(cpu);

    // Push flags with B and U both set in the pushed copy only.
    cpu.write(SP(cpu), cpu.flags.reg | 0x30);
    DEC_SP(cpu);

    SET_FLAG(I, 1);

    uint16_t addr = (uint16_t)cpu.read(0xFFFE) | ((uint16_t)cpu.read(0xFFFF) << 8);
    cpu.PC = addr;
    return 0;
}

// branch on V = 0                  N Z C I D V
//                                  - - - - - -
static uint8_t BVC() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (!GET_FLAG(V)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// branch on V = 1                  N Z C I D V
//                                  - - - - - -
static uint8_t BVS() {
    uint8_t cycles = 0;
    uint16_t old_pc = cpu.PC;

    if (GET_FLAG(V)) {
        // One extra cycle if the branch is taken
        cycles++;

        cpu.PC += cpu.operand_addr;

        // One extra cycle if the branch crosses a page
        if ((cpu.PC & 0xff00) != (old_pc & 0xff00))
            cpu.cycles++;
    }
    return cycles;
}

// 0 -> C                           N Z C I D V
//                                  - - 0 - - -
static uint8_t CLC() {
    SET_FLAG(C, 0);
    return 0;
}

// 0 -> D                           N Z C I D V
//                                  - - - - 0 -
static uint8_t CLD() {
    SET_FLAG(D, 0);
    return 0;
}

// 0 -> I                           N Z C I D V
//                                  - - - 0 - -
static uint8_t CLI() {
    SET_FLAG(I, 0);
    return 0;
}

// 0 -> V                           N Z C I D V
//                                  - - - - - 0
static uint8_t CLV() {
    SET_FLAG(V, 0);
    return 0;
}

// A - M                            N Z C I D V
//                                  + + + - - -
static uint8_t CMP() {
    uint16_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);

    tmp = (uint16_t)cpu.A - (uint16_t)cpu.operand;
    SET_FLAG(C, (cpu.A >= cpu.operand) ? 1 : 0);

    // Set flags
    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!(tmp & 0xff)));

    return 0;
}

// X - M                            N Z C I D V
//                                  + + + - - -
static uint8_t CPX() {
    uint8_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);

    tmp = cpu.X - cpu.operand;

    SET_FLAG(N, (tmp & 0x80));
    tmp &= 0x00FF;
    SET_FLAG(Z, (!tmp));
    SET_FLAG(C, (cpu.X >= cpu.operand));

    return 0;
}

// Y - M                            N Z C I D V
//                                  + + + - - -
static uint8_t CPY() {
    uint8_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);

    tmp = cpu.Y - cpu.operand;

    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!tmp));
    SET_FLAG(C, (cpu.Y >= cpu.operand));
    return 0;
}

// M - 1 -> M                       N Z C I D V
//                                  + + - - - -
static uint8_t DEC() {
    uint8_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);

    tmp = (uint16_t)cpu.operand - 1;

    cpu.write(cpu.operand_addr, tmp & 0xFF);

    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!tmp));

    return 0;
}

// X - 1 -> X                       N Z C I D V
//                                  + + - - - -
static uint8_t DEX() {
    cpu.X--;

    SET_FLAG(N, (cpu.X & 0x80));
    SET_FLAG(Z, (!cpu.X));

    return 0;
}

// Y - 1 -> Y                       N Z C I D V
//                                  + + - - - -
static uint8_t DEY() {
    cpu.Y--;

    SET_FLAG(N, (cpu.Y & 0x80));
    SET_FLAG(Z, (!cpu.Y));

    return 0;
}

// A EOR M -> A                     N Z C I D V
//                                  + + - - - -
static uint8_t EOR() {
    cpu.operand = cpu.read(cpu.operand_addr);

    cpu.A = cpu.A ^ cpu.operand;

    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));

    return 0;
}

// M + 1 -> M                       N Z C I D V
//                                  + + - - - -
static uint8_t INC() {
    uint8_t tmp;

    cpu.operand = cpu.read(cpu.operand_addr);
    log_print("INC read %02x from %04x\n", cpu.operand, cpu.operand_addr);

    tmp = (uint16_t)cpu.operand + 1;

    cpu.write(cpu.operand_addr, tmp & 0xFF);
    log_print("INC wrote %02x to %04x\n", tmp & 0xFF, cpu.operand_addr);

    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!tmp));

    return 0;
}

// X + 1 -> X                       N Z C I D V
//                                  + + - - - -
static uint8_t INX() {
    cpu.X++;

    SET_FLAG(N, (cpu.X & 0x80));
    SET_FLAG(Z, (!cpu.X));

    return 0;
}

// Y + 1 -> Y                       N Z C I D V
//                                  + + - - - -
static uint8_t INY() {
    cpu.Y++;

    SET_FLAG(N, (cpu.Y & 0x80));
    SET_FLAG(Z, (!cpu.Y));

    return 0;
}

// (PC+1) -> PCL                    N Z C I D V
// (PC+2) -> PCH                    - - - - - -
static uint8_t JMP() {
    cpu.PC = cpu.operand_addr;
    return 0;
}

// push (PC+2),                     N Z C I D V
// (PC+1) -> PCL                    - - - - - -
// (PC+2) -> PCH
static uint8_t JSR() {
    uint16_t tmp = cpu.PC - 1;
    cpu.write(SP(cpu), (tmp >> 8) & 0x00FF);
    DEC_SP(cpu);
    cpu.write(SP(cpu), (tmp & 0x00FF));
    DEC_SP(cpu);
    cpu.PC = cpu.operand_addr;

    return 0;
}

// M -> A                           N Z C I D V
//                                  + + - - - -
static uint8_t LDA() {
    cpu.operand = cpu.read(cpu.operand_addr);
    cpu.A = cpu.operand;

    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));

    return 0;
}

// M -> X                           N Z C I D V
//                                  + + - - - -
static uint8_t LDX() {
    cpu.operand = cpu.read(cpu.operand_addr);

    cpu.X = cpu.operand;

    SET_FLAG(N, (cpu.X & 0x80));
    SET_FLAG(Z, (!cpu.X));

    return 0;
}

// M -> Y                           N Z C I D V
//                                  + + - - - -
static uint8_t LDY() {
    cpu.operand = cpu.read(cpu.operand_addr);

    cpu.Y = cpu.operand;

    SET_FLAG(N, (cpu.Y & 0x80));
    SET_FLAG(Z, (!cpu.Y));

    return 0;
}

// 0 -> [76543210] -> C             N Z C I D V
//                                  0 + + - - -
static uint8_t LSR() {
    uint8_t tmp;

    if ((cpu.curr_insn->addr_mode != IMP) && (cpu.curr_insn->addr_mode != ACC))
        cpu.operand = cpu.read(cpu.operand_addr);

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.operand = cpu.A;

    SET_FLAG(C, (cpu.operand & 0x1));

    tmp = cpu.operand >> 1;

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.A = tmp;
    else
        cpu.write(cpu.operand_addr, (tmp));

    SET_FLAG(Z, (!tmp));
    SET_FLAG(N, 0);

    return 0;
}

// ---                              N Z C I D V
//                                  - - - - - -
static uint8_t NOP() { return 0; }

// A OR M -> A                      N Z C I D V
//                                  + + - - - -
static uint8_t ORA() {
    cpu.operand = cpu.read(cpu.operand_addr);

    cpu.A |= cpu.operand;

    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));
    return 0;
}

// push A                           N Z C I D V
//                                  - - - - - -
static uint8_t PHA() {
    cpu.write(SP(cpu), cpu.A);
    DEC_SP(cpu);
    return 0;
}

// push SR                          N Z C I D V
//                                  - - - - - -
static uint8_t PHP() {
    // printf("PHP called, flags: %02x\n", cpu.flags.reg);
    SET_FLAG(B, 1); // Set B flag when pushing to stack from BRK or PHP
    cpu.write(SP(cpu), cpu.flags.reg);
    DEC_SP(cpu);
    return 0;
}

// pull A                           N Z C I D V
//                                  + + - - - -
static uint8_t PLA() {
    INC_SP(cpu);
    cpu.A = cpu.read(SP(cpu));

    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));

    return 0;
}

// pull SR                          N Z C I D V
//                                  from stack
static uint8_t PLP() {
    INC_SP(cpu);
    cpu.flags.reg = cpu.read(SP(cpu));
    return 0;
}

// C <- [76543210] <- C             N Z C I D V
//                                  + + + - - -
static uint8_t ROL() {

    uint8_t tmp;
    uint8_t old_carry = GET_FLAG(C);

    if ((cpu.curr_insn->addr_mode != IMP) && (cpu.curr_insn->addr_mode != ACC))
        cpu.operand = cpu.read(cpu.operand_addr);

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.operand = cpu.A;

    SET_FLAG(C, (cpu.operand & 0x80) >> 7);

    tmp = cpu.operand << 1 | old_carry;

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.A = tmp;
    else
        cpu.write(cpu.operand_addr, (tmp));

    // Set flags
    SET_FLAG(N, (tmp & 0x80));
    SET_FLAG(Z, (!tmp));

    return 0;
}

// C -> [76543210] -> C             N Z C I D V
//                                  + + + - - -
static uint8_t ROR() {
    uint8_t tmp;
    uint8_t old_carry = GET_FLAG(C);

    if ((cpu.curr_insn->addr_mode != IMP) && (cpu.curr_insn->addr_mode != ACC))
        cpu.operand = cpu.read(cpu.operand_addr);

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.operand = cpu.A;

    SET_FLAG(C, (cpu.operand & 0x1));

    tmp = cpu.operand >> 1 | (old_carry << 7);

    if (cpu.curr_insn->addr_mode == ACC)
        cpu.A = tmp;
    else
        cpu.write(cpu.operand_addr, (tmp));

    tmp &= 0x00FF;
    SET_FLAG(Z, (!tmp));
    SET_FLAG(N, tmp & 0x80);

    return 0;
}

// pull SR, pull PC                 N Z C I D V
//                                  from stack
static uint8_t RTI() {
    uint16_t tmp;

    INC_SP(cpu);
    cpu.flags.reg = cpu.read(SP(cpu));

    INC_SP(cpu);
    tmp = cpu.read(SP(cpu));
    INC_SP(cpu);
    tmp |= (cpu.read(SP(cpu)) << 8);

    cpu.PC = tmp;

    return 0;
}

// pull PC, PC+1 -> PC              N Z C I D V
//                                  - - - - - -
static uint8_t RTS() {
    uint16_t tmp;

    INC_SP(cpu);
    tmp = cpu.read(SP(cpu));
    INC_SP(cpu);
    tmp |= (cpu.read(SP(cpu)) << 8);

    cpu.PC = tmp + 1;

    return 0;
}

// A - M - C -> A                   N Z C I D V
//                                  + + + - - +
static uint8_t SBC() {
    uint16_t tmp;
    uint16_t value;

    cpu.operand = cpu.read(cpu.operand_addr);

    value = ((uint16_t)cpu.operand) ^ 0x00ff;

    tmp = (uint16_t)cpu.A + value + (uint16_t)GET_FLAG(C);
    SET_FLAG(C, (tmp & 0xFF00));

    // Set flags
    SET_FLAG(N, (tmp & 0x0080));
    tmp &= 0x00FF;
    SET_FLAG(Z, (!tmp));
    SET_FLAG(V, ((tmp ^ (uint16_t)cpu.A) & (tmp ^ value) & 0x0080));

    cpu.A = tmp & 0x00FF;

    return 0;
}

// 1 -> C                           N Z C I D V
//                                  - - 1 - - -
static uint8_t SEC() {
    SET_FLAG(C, 1);
    return 0;
}

// 1 -> D                           N Z C I D V
//                                  - - - - 1 -
static uint8_t SED() {
    SET_FLAG(D, 1);
    return 0;
}

// 1 -> I                           N Z C I D V
//                                  - - - 1 - -
static uint8_t SEI() {
    SET_FLAG(I, 1);
    return 0;
}

// A -> M                           N Z C I D V
//                                  - - - - - -
static uint8_t STA() {
    cpu.write(cpu.operand_addr, cpu.A);
    return 0;
}

// X -> M                           N Z C I D V
//                                  - - - - - -
static uint8_t STX() {
    cpu.write(cpu.operand_addr, cpu.X);
    return 0;
}

// Y -> M                           N Z C I D V
//                                  - - - - - -
static uint8_t STY() {
    cpu.write(cpu.operand_addr, cpu.Y);
    return 0;
}

// A -> X                           N Z C I D V
//                                  + + - - - -
static uint8_t TAX() {
    cpu.X = cpu.A;
    SET_FLAG(N, (cpu.X & 0x80));
    SET_FLAG(Z, (!cpu.X));

    return 0;
}

// A -> Y                           N Z C I D V
//                                  + + - - - -
static uint8_t TAY() {
    cpu.Y = cpu.A;
    SET_FLAG(N, (cpu.Y & 0x80));
    SET_FLAG(Z, (!cpu.Y));

    return 0;
}

// SP -> X                          N Z C I D V
//                                  + + - - - -
static uint8_t TSX() {
    cpu.X = (uint8_t)SP(cpu);
    SET_FLAG(N, (cpu.X & 0x80));
    SET_FLAG(Z, (!cpu.X));

    return 0;
}

// X -> A                           N Z C I D V
//                                  + + - - - -
static uint8_t TXA() {
    cpu.A = cpu.X;
    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));

    return 0;
}

// X -> SP                          N Z C I D V
//                                  - - - - - -
static uint8_t TXS() {
    SET_SP(cpu, cpu.X);

    return 0;
}

// Y -> A                           N Z C I D V
//                                  + + - - - -
static uint8_t TYA() {
    cpu.A = cpu.Y;
    SET_FLAG(N, (cpu.A & 0x80));
    SET_FLAG(Z, (!cpu.A));

    return 0;
}

static uint8_t XXX() {
    log_print("Invalid opcode encountered\n");
#ifndef INVALID_AS_NOP
    exit(1);
#endif
    return 1;
}

/* -------------------------------------------------------------------------
 * Illegal (undocumented) opcode implementations
 * Reference: https://www.nesdev.org/wiki/CPU_unofficial_opcodes
 * ------------------------------------------------------------------------- */

/* SLO: ASL mem, then ORA A with result */
static uint8_t SLO() {
    uint8_t m = cpu.read(cpu.operand_addr);
    uint8_t res = m << 1;
    cpu.write(cpu.operand_addr, res);
    SET_FLAG(C, m & 0x80);
    cpu.A |= res;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* RLA: ROL mem, then AND A with result */
static uint8_t RLA() {
    uint8_t m = cpu.read(cpu.operand_addr);
    uint8_t res = (uint8_t)((m << 1) | GET_FLAG(C));
    SET_FLAG(C, m & 0x80);
    cpu.write(cpu.operand_addr, res);
    cpu.A &= res;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* SRE: LSR mem, then EOR A with result */
static uint8_t SRE() {
    uint8_t m = cpu.read(cpu.operand_addr);
    uint8_t res = m >> 1;
    cpu.write(cpu.operand_addr, res);
    SET_FLAG(C, m & 0x01);
    cpu.A ^= res;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* RRA: ROR mem, then ADC A with result */
static uint8_t RRA() {
    uint8_t m = cpu.read(cpu.operand_addr);
    uint8_t res = (uint8_t)((m >> 1) | (GET_FLAG(C) << 7));
    SET_FLAG(C, m & 0x01);
    cpu.write(cpu.operand_addr, res);
    /* ADC logic */
    uint16_t tmp = (uint16_t)cpu.A + (uint16_t)res + (uint16_t)GET_FLAG(C);
    SET_FLAG(C, tmp > 0xFF);
    SET_FLAG(V, (~((uint16_t)cpu.A ^ (uint16_t)res) &
                 ((uint16_t)cpu.A ^ tmp)) & 0x0080);
    cpu.A = (uint8_t)tmp;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* SAX: store A & X to memory (no flag changes) */
static uint8_t SAX() {
    cpu.write(cpu.operand_addr, cpu.A & cpu.X);
    return 0;
}

/* LAX: load both A and X from memory */
static uint8_t LAX() {
    cpu.A = cpu.X = cpu.read(cpu.operand_addr);
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* DCP: DEC mem, then CMP A with result */
static uint8_t DCP() {
    uint8_t m = cpu.read(cpu.operand_addr);
    uint8_t res = m - 1;
    cpu.write(cpu.operand_addr, res);
    uint16_t tmp = (uint16_t)cpu.A - (uint16_t)res;
    SET_FLAG(C, cpu.A >= res);
    SET_FLAG(N, tmp & 0x80);
    SET_FLAG(Z, !(tmp & 0xFF));
    return 0;
}

/* ISB: INC mem, then SBC A with result */
static uint8_t ISB() {
    uint8_t m = cpu.read(cpu.operand_addr);
    uint8_t inc = m + 1;
    cpu.write(cpu.operand_addr, inc);
    /* SBC: A - inc - (1-C) == A + ~inc + C */
    uint16_t val = (uint16_t)inc ^ 0x00FF;
    uint16_t tmp = (uint16_t)cpu.A + val + (uint16_t)GET_FLAG(C);
    SET_FLAG(C, tmp & 0xFF00);
    SET_FLAG(V, ((tmp ^ (uint16_t)cpu.A) & (tmp ^ val)) & 0x0080);
    cpu.A = (uint8_t)tmp;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* ANC: AND A with immediate, then copy bit 7 to carry */
static uint8_t ANC() {
    cpu.operand = cpu.read(cpu.operand_addr);
    cpu.A &= cpu.operand;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    SET_FLAG(C, cpu.A & 0x80);
    return 0;
}

/* ALR: AND A with immediate, then LSR A */
static uint8_t ALR() {
    cpu.operand = cpu.read(cpu.operand_addr);
    cpu.A &= cpu.operand;
    SET_FLAG(C, cpu.A & 0x01);
    cpu.A >>= 1;
    SET_FLAG(N, 0);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* ARR: AND A with immediate, then ROR A (special carry/overflow) */
static uint8_t ARR() {
    cpu.operand = cpu.read(cpu.operand_addr);
    cpu.A = (uint8_t)(((cpu.A & cpu.operand) >> 1) | (GET_FLAG(C) << 7));
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    SET_FLAG(C, (cpu.A >> 6) & 1);
    SET_FLAG(V, ((cpu.A >> 6) ^ (cpu.A >> 5)) & 1);
    return 0;
}

/* XAA: A = A & X & immediate (unstable; common approximation) */
static uint8_t XAA() {
    cpu.operand = cpu.read(cpu.operand_addr);
    cpu.A = cpu.A & cpu.X & cpu.operand;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* SBX (AXS): X = (A & X) - immediate, set NZC like CMP */
static uint8_t SBX() {
    cpu.operand = cpu.read(cpu.operand_addr);
    uint8_t ax = cpu.A & cpu.X;
    uint8_t res = ax - cpu.operand;
    SET_FLAG(C, ax >= cpu.operand);
    cpu.X = res;
    SET_FLAG(N, cpu.X & 0x80);
    SET_FLAG(Z, !cpu.X);
    return 0;
}

/* USB (USBC): same as SBC #imm */
static uint8_t USB() {
    cpu.operand = cpu.read(cpu.operand_addr);
    uint16_t val = (uint16_t)cpu.operand ^ 0x00FF;
    uint16_t tmp = (uint16_t)cpu.A + val + (uint16_t)GET_FLAG(C);
    SET_FLAG(C, tmp & 0xFF00);
    SET_FLAG(V, ((tmp ^ (uint16_t)cpu.A) & (tmp ^ val)) & 0x0080);
    cpu.A = (uint8_t)tmp;
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

/* AHX: store A & X & (addr_high + 1) — highly unstable hardware behaviour */
static uint8_t AHX() {
    uint8_t h = (uint8_t)((cpu.operand_addr >> 8) + 1);
    cpu.write(cpu.operand_addr, cpu.A & cpu.X & h);
    return 0;
}

/* TAS: S = A & X; store S & (addr_high + 1) */
static uint8_t TAS() {
    SET_SP(cpu, cpu.A & cpu.X);
    uint8_t h = (uint8_t)((cpu.operand_addr >> 8) + 1);
    cpu.write(cpu.operand_addr, cpu.A & cpu.X & h);
    return 0;
}


/* LAS: A = X = SP = SP & mem[addr] */
static uint8_t LAS() {
    cpu.operand = cpu.read(cpu.operand_addr);
    cpu.A = cpu.X = (uint8_t)cpu.sp;
    cpu.A &= cpu.operand;
    cpu.X = cpu.A;
    SET_SP(cpu, cpu.A);
    SET_FLAG(N, cpu.A & 0x80);
    SET_FLAG(Z, !cpu.A);
    return 0;
}

static void print_regs() {
    log_print("A: %02X\n", cpu.A);
    log_print("X: %02X\n", cpu.X);
    log_print("Y: %02X\n", cpu.Y);
    log_print("SP: %04X\n", SP(cpu));
    log_print("PC: %04X\n", cpu.PC);
    log_print("FLAGS: %02X\n", cpu.flags.reg);
    log_print("N V U B D I Z C\n");
    log_print("%d %d %d %d %d %d %d %d\n", GET_FLAG(N), GET_FLAG(V),
              GET_FLAG(U), GET_FLAG(B), GET_FLAG(D), GET_FLAG(I), GET_FLAG(Z),
              GET_FLAG(C));
}

// https://wiki.nesdev.com/w/index.php/CPU_interrupts#IRQ_and_NMI_tick-by-tick_execution
static void nmi(void) {
    uint16_t vector = 0xFFFA;
    uint16_t addr;

    // Push PC
    cpu.write(SP(cpu), (cpu.PC >> 8) & 0x00FF);
    DEC_SP(cpu);
    cpu.write(SP(cpu), (cpu.PC & 0x00FF));
    DEC_SP(cpu);

    // Clear B, set I
    SET_FLAG(B, 0);
    // Push SR
    cpu.write(SP(cpu), cpu.flags.reg);
    DEC_SP(cpu);
    SET_FLAG(I, 1);

    // Jmp to NMI vector
    addr = cpu.read(vector);
    addr |= (cpu.read(vector + 1) << 8);

    cpu.PC = addr;
}

static void irq(void) {
    uint16_t vector = 0xFFFE;
    uint16_t addr;

    if (!GET_FLAG(I)) {
        // Push PC
        cpu.write(SP(cpu), (cpu.PC >> 8) & 0x00FF);
        DEC_SP(cpu);
        cpu.write(SP(cpu), (cpu.PC & 0x00FF));
        DEC_SP(cpu);

        // Push SR with B clear (IRQ does not set B in pushed copy)
        cpu.write(SP(cpu), (cpu.flags.reg | 0x20) & ~0x10);
        DEC_SP(cpu);
        SET_FLAG(I, 1);

        addr = cpu.read(vector);
        addr |= ((uint16_t)cpu.read(vector + 1) << 8);

        cpu.PC = addr;
    }
}

static void reset(void) {
    cpu.flags.reg = 0;
    SET_FLAG(U, 1); // U always 1
    SET_FLAG(I, 1); // interrupts disabled on reset
    SET_SP(cpu, 0xfd);
    cpu.PC = (uint16_t)cpu.read(0xFFFC) | ((uint16_t)cpu.read(0xFFFD) << 8);
}

static uint8_t read(uint16_t addr) { return cpu.bus->read(addr); }

static void write(uint16_t addr, uint8_t data) {
    log_print("CPU RAM WRITE: %02x to %04x\n", data, addr);
    cpu.bus->write(addr, data);
    return;
}

static uint8_t fetch() {
    uint8_t a, b, c;

    // DEBUG
    cpu.start_pc = cpu.PC;

    cpu.opcode = cpu.bus->read(cpu.PC++);
    a = DECODE_A(cpu.opcode);
    b = DECODE_B(cpu.opcode);
    c = DECODE_C(cpu.opcode);

    cpu.curr_insn = &instruction_table[c][a][b];

    // Set the initial cycle count
    cpu.cycles = cpu.curr_insn->cycles;

    // Calling addr_mode will resolve operand and any addresses
    // as well as determine any additional cycles to be added on
    // for memory access types. Branc instructions can incur
    // additional cycles but need to be resolved at execution
    cpu.curr_insn->addr_mode();

    return cpu.opcode;
}

static uint8_t execute() {
    cpu.cycles += cpu.curr_insn->execute();
    return 0;
}

static void clock() {
    // OAM DMA stall: CPU halts for 513-514 cycles while DMA copies 256 bytes.
    if (cpu.bus && cpu.bus->dma_halt_cycles > 0) {
        cpu.bus->dma_halt_cycles--;
        return;
    }

    if (cpu.cycles == 0) {
        // Check for NMI before fetching next instruction.
        // NMI is edge-triggered and can't be disabled.
        if (cpu.bus && cpu.bus->ppu && cpu.bus->ppu->nmi_triggered) {
            cpu.bus->ppu->nmi_triggered = 0;
            cpu.nmi();
            cpu.cycles = 7;
            return;
        }

        // Convert a deferred NMI (from a PPUCTRL 0→1 write that happened
        // mid-instruction) into a triggered NMI.  The promotion happens AFTER
        // the nmi_triggered check so the NMI fires at the start of the
        // instruction *after* this one — matching hardware behaviour where the
        // NMI caused by a PPUCTRL write is delayed until after the next
        // instruction completes.
        if (cpu.bus && cpu.bus->ppu && cpu.bus->ppu->nmi_deferred) {
            cpu.bus->ppu->nmi_triggered = 1;
            cpu.bus->ppu->nmi_deferred  = 0;
        }

        // Check for mapper IRQ (level-triggered: stays asserted until mapper
        // acknowledges it, typically via a write to its IRQ-disable register).
        if (!GET_FLAG(I) &&
            cpu.bus && cpu.bus->cart && cpu.bus->cart->map &&
            cpu.bus->cart->map->irq_pending) {
            cpu.irq();
            cpu.cycles = 7;
            return;
        }

        cpu.fetch();
        cpu.execute();

#ifdef NES_DEBUG
        {
            uint8_t buf[0x100];
            log_print("%04x: %02x %s %04x / %02x\n", cpu.start_pc, cpu.opcode,
                      cpu.curr_insn->mnem, cpu.operand_addr, cpu.operand);
            cpu.print_regs();
            cpu.bus->debug_read(SP(cpu) - 0x10, buf, 0x20);
            log_print("Stack:\n");
            hex_dump(buf, 0x20);
            cpu.bus->debug_read(cpu.PC, buf, 0x10);
            log_print("%04x: \n", cpu.PC);
            hex_dump(buf, 0x10);
            cpu.bus->debug_read(0, buf, 0x20);
            log_print("%04x: \n", 0);
            hex_dump(buf, 0x20);
            log_print("\n");
        }
#endif
    }
    cpu.cycles--;
}

static void connect_bus(void *bus) { cpu.bus = (struct nesbus *)bus; }

struct cpu6502 *cpu6502_init() {
    cpu.nmi = nmi;
    cpu.irq = irq;
    cpu.reset = reset;
    cpu.read = read;
    cpu.write = write;
    cpu.fetch = fetch;
    cpu.execute = execute;
    cpu.clock = clock;
    cpu.connect_bus = connect_bus;
    cpu.print_regs = print_regs;

    return &cpu;
}
