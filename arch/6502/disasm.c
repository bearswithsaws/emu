#include "disasm.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    M_IMP = 0, /* Implied          — 1 byte               */
    M_ACC,     /* Accumulator      — 1 byte  "A"          */
    M_IMM,     /* Immediate        — 2 bytes "#$xx"       */
    M_ZPG,     /* Zero Page        — 2 bytes "$xx"        */
    M_ZPX,     /* Zero Page,X      — 2 bytes "$xx,X"      */
    M_ZPY,     /* Zero Page,Y      — 2 bytes "$xx,Y"      */
    M_ABS,     /* Absolute         — 3 bytes "$xxxx"      */
    M_ABX,     /* Absolute,X       — 3 bytes "$xxxx,X"    */
    M_ABY,     /* Absolute,Y       — 3 bytes "$xxxx,Y"    */
    M_IND,     /* Indirect         — 3 bytes "($xxxx)"    */
    M_IDX,     /* (Indirect,X)     — 2 bytes "($xx,X)"   */
    M_IDY,     /* (Indirect),Y     — 2 bytes "($xx),Y"   */
    M_REL,     /* Relative         — 2 bytes "$xxxx"      */
} mode_t;

typedef struct {
    const char *mnem;
    mode_t      mode;
} opinfo_t;

/* Flat 256-entry table, indexed by opcode byte.
 * Derived from the 3D instruction table in 6502.c using opcode = (a<<5)|(b<<2)|c.
 * Illegal/undocumented opcodes match the mnemonics used by the CPU emulator. */
static const opinfo_t op_table[256] = {
    /* $00-$0F */
    {"BRK", M_IMP}, {"ORA", M_IDX}, {"???", M_IMP}, {"SLO", M_IDX},
    {"NOP", M_ZPG}, {"ORA", M_ZPG}, {"ASL", M_ZPG}, {"SLO", M_ZPG},
    {"PHP", M_IMP}, {"ORA", M_IMM}, {"ASL", M_ACC}, {"ANC", M_IMM},
    {"NOP", M_ABS}, {"ORA", M_ABS}, {"ASL", M_ABS}, {"SLO", M_ABS},
    /* $10-$1F */
    {"BPL", M_REL}, {"ORA", M_IDY}, {"???", M_IMP}, {"SLO", M_IDY},
    {"NOP", M_ZPX}, {"ORA", M_ZPX}, {"ASL", M_ZPX}, {"SLO", M_ZPX},
    {"CLC", M_IMP}, {"ORA", M_ABY}, {"NOP", M_IMP}, {"SLO", M_ABY},
    {"NOP", M_ABX}, {"ORA", M_ABX}, {"ASL", M_ABX}, {"SLO", M_ABX},
    /* $20-$2F */
    {"JSR", M_ABS}, {"AND", M_IDX}, {"???", M_IMP}, {"RLA", M_IDX},
    {"BIT", M_ZPG}, {"AND", M_ZPG}, {"ROL", M_ZPG}, {"RLA", M_ZPG},
    {"PLP", M_IMP}, {"AND", M_IMM}, {"ROL", M_ACC}, {"ANC", M_IMM},
    {"BIT", M_ABS}, {"AND", M_ABS}, {"ROL", M_ABS}, {"RLA", M_ABS},
    /* $30-$3F */
    {"BMI", M_REL}, {"AND", M_IDY}, {"???", M_IMP}, {"RLA", M_IDY},
    {"NOP", M_ZPX}, {"AND", M_ZPX}, {"ROL", M_ZPX}, {"RLA", M_ZPX},
    {"SEC", M_IMP}, {"AND", M_ABY}, {"NOP", M_IMP}, {"RLA", M_ABY},
    {"NOP", M_ABX}, {"AND", M_ABX}, {"ROL", M_ABX}, {"RLA", M_ABX},
    /* $40-$4F */
    {"RTI", M_IMP}, {"EOR", M_IDX}, {"???", M_IMP}, {"SRE", M_IDX},
    {"NOP", M_ZPG}, {"EOR", M_ZPG}, {"LSR", M_ZPG}, {"SRE", M_ZPG},
    {"PHA", M_IMP}, {"EOR", M_IMM}, {"LSR", M_ACC}, {"ALR", M_IMM},
    {"JMP", M_ABS}, {"EOR", M_ABS}, {"LSR", M_ABS}, {"SRE", M_ABS},
    /* $50-$5F */
    {"BVC", M_REL}, {"EOR", M_IDY}, {"???", M_IMP}, {"SRE", M_IDY},
    {"NOP", M_ZPX}, {"EOR", M_ZPX}, {"LSR", M_ZPX}, {"SRE", M_ZPX},
    {"CLI", M_IMP}, {"EOR", M_ABY}, {"NOP", M_IMP}, {"SRE", M_ABY},
    {"NOP", M_ABX}, {"EOR", M_ABX}, {"LSR", M_ABX}, {"SRE", M_ABX},
    /* $60-$6F */
    {"RTS", M_IMP}, {"ADC", M_IDX}, {"???", M_IMP}, {"RRA", M_IDX},
    {"NOP", M_ZPG}, {"ADC", M_ZPG}, {"ROR", M_ZPG}, {"RRA", M_ZPG},
    {"PLA", M_IMP}, {"ADC", M_IMM}, {"ROR", M_ACC}, {"ARR", M_IMM},
    {"JMP", M_IND}, {"ADC", M_ABS}, {"ROR", M_ABS}, {"RRA", M_ABS},
    /* $70-$7F */
    {"BVS", M_REL}, {"ADC", M_IDY}, {"???", M_IMP}, {"RRA", M_IDY},
    {"NOP", M_ZPX}, {"ADC", M_ZPX}, {"ROR", M_ZPX}, {"RRA", M_ZPX},
    {"SEI", M_IMP}, {"ADC", M_ABY}, {"NOP", M_IMP}, {"RRA", M_ABY},
    {"NOP", M_ABX}, {"ADC", M_ABX}, {"ROR", M_ABX}, {"RRA", M_ABX},
    /* $80-$8F */
    {"NOP", M_IMM}, {"STA", M_IDX}, {"NOP", M_IMM}, {"SAX", M_IDX},
    {"STY", M_ZPG}, {"STA", M_ZPG}, {"STX", M_ZPG}, {"SAX", M_ZPG},
    {"DEY", M_IMP}, {"NOP", M_IMM}, {"TXA", M_IMP}, {"XAA", M_IMM},
    {"STY", M_ABS}, {"STA", M_ABS}, {"STX", M_ABS}, {"SAX", M_ABS},
    /* $90-$9F */
    {"BCC", M_REL}, {"STA", M_IDY}, {"???", M_IMP}, {"AHX", M_IDY},
    {"STY", M_ZPX}, {"STA", M_ZPX}, {"STX", M_ZPY}, {"SAX", M_ZPY},
    {"TYA", M_IMP}, {"STA", M_ABY}, {"TXS", M_IMP}, {"TAS", M_ABY},
    {"NOP", M_ABX}, {"STA", M_ABX}, {"???", M_IMP}, {"AHX", M_ABY},
    /* $A0-$AF */
    {"LDY", M_IMM}, {"LDA", M_IDX}, {"LDX", M_IMM}, {"LAX", M_IDX},
    {"LDY", M_ZPG}, {"LDA", M_ZPG}, {"LDX", M_ZPG}, {"LAX", M_ZPG},
    {"TAY", M_IMP}, {"LDA", M_IMM}, {"TAX", M_IMP}, {"LAX", M_IMM},
    {"LDY", M_ABS}, {"LDA", M_ABS}, {"LDX", M_ABS}, {"LAX", M_ABS},
    /* $B0-$BF */
    {"BCS", M_REL}, {"LDA", M_IDY}, {"???", M_IMP}, {"LAX", M_IDY},
    {"LDY", M_ZPX}, {"LDA", M_ZPX}, {"LDX", M_ZPY}, {"LAX", M_ZPY},
    {"CLV", M_IMP}, {"LDA", M_ABY}, {"TSX", M_IMP}, {"LAS", M_ABY},
    {"LDY", M_ABX}, {"LDA", M_ABX}, {"LDX", M_ABY}, {"LAX", M_ABY},
    /* $C0-$CF */
    {"CPY", M_IMM}, {"CMP", M_IDX}, {"NOP", M_IMM}, {"DCP", M_IDX},
    {"CPY", M_ZPG}, {"CMP", M_ZPG}, {"DEC", M_ZPG}, {"DCP", M_ZPG},
    {"INY", M_IMP}, {"CMP", M_IMM}, {"DEX", M_IMP}, {"SBX", M_IMM},
    {"CPY", M_ABS}, {"CMP", M_ABS}, {"DEC", M_ABS}, {"DCP", M_ABS},
    /* $D0-$DF */
    {"BNE", M_REL}, {"CMP", M_IDY}, {"???", M_IMP}, {"DCP", M_IDY},
    {"NOP", M_ZPX}, {"CMP", M_ZPX}, {"DEC", M_ZPX}, {"DCP", M_ZPX},
    {"CLD", M_IMP}, {"CMP", M_ABY}, {"NOP", M_IMP}, {"DCP", M_ABY},
    {"NOP", M_ABX}, {"CMP", M_ABX}, {"DEC", M_ABX}, {"DCP", M_ABX},
    /* $E0-$EF */
    {"CPX", M_IMM}, {"SBC", M_IDX}, {"NOP", M_IMM}, {"ISB", M_IDX},
    {"CPX", M_ZPG}, {"SBC", M_ZPG}, {"INC", M_ZPG}, {"ISB", M_ZPG},
    {"INX", M_IMP}, {"SBC", M_IMM}, {"NOP", M_IMP}, {"USB", M_IMM},
    {"CPX", M_ABS}, {"SBC", M_ABS}, {"INC", M_ABS}, {"ISB", M_ABS},
    /* $F0-$FF */
    {"BEQ", M_REL}, {"SBC", M_IDY}, {"???", M_IMP}, {"ISB", M_IDY},
    {"NOP", M_ZPX}, {"SBC", M_ZPX}, {"INC", M_ZPX}, {"ISB", M_ZPX},
    {"SED", M_IMP}, {"SBC", M_ABY}, {"NOP", M_IMP}, {"ISB", M_ABY},
    {"NOP", M_ABX}, {"SBC", M_ABX}, {"INC", M_ABX}, {"ISB", M_ABX},
};

/* Instruction byte lengths per addressing mode. */
static const int mode_len[13] = {
    1, /* M_IMP */ 1, /* M_ACC */ 2, /* M_IMM */ 2, /* M_ZPG */
    2, /* M_ZPX */ 2, /* M_ZPY */ 3, /* M_ABS */ 3, /* M_ABX */
    3, /* M_ABY */ 3, /* M_IND */ 2, /* M_IDX */ 2, /* M_IDY */
    2, /* M_REL */
};

int disasm_insn(uint16_t addr, const uint8_t bytes[3], char *out, size_t out_len) {
    uint8_t opcode   = bytes[0];
    uint8_t op8      = bytes[1];
    uint16_t op16    = (uint16_t)(bytes[1] | (bytes[2] << 8));
    const opinfo_t *info = &op_table[opcode];
    int len = mode_len[info->mode];

    switch (info->mode) {
    case M_IMP:
        snprintf(out, out_len, "%s", info->mnem);
        break;
    case M_ACC:
        snprintf(out, out_len, "%s A", info->mnem);
        break;
    case M_IMM:
        snprintf(out, out_len, "%s #$%02X", info->mnem, op8);
        break;
    case M_ZPG:
        snprintf(out, out_len, "%s $%02X", info->mnem, op8);
        break;
    case M_ZPX:
        snprintf(out, out_len, "%s $%02X,X", info->mnem, op8);
        break;
    case M_ZPY:
        snprintf(out, out_len, "%s $%02X,Y", info->mnem, op8);
        break;
    case M_ABS:
        snprintf(out, out_len, "%s $%04X", info->mnem, op16);
        break;
    case M_ABX:
        snprintf(out, out_len, "%s $%04X,X", info->mnem, op16);
        break;
    case M_ABY:
        snprintf(out, out_len, "%s $%04X,Y", info->mnem, op16);
        break;
    case M_IND:
        snprintf(out, out_len, "%s ($%04X)", info->mnem, op16);
        break;
    case M_IDX:
        snprintf(out, out_len, "%s ($%02X,X)", info->mnem, op8);
        break;
    case M_IDY:
        snprintf(out, out_len, "%s ($%02X),Y", info->mnem, op8);
        break;
    case M_REL: {
        /* Branch target = addr + 2 + signed offset */
        uint16_t target = (uint16_t)(addr + 2 + (int8_t)op8);
        snprintf(out, out_len, "%s $%04X", info->mnem, target);
        break;
    }
    }

    return len;
}
