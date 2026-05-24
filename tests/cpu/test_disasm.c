/*
 * test_disasm.c
 *
 * Unit tests for the 6502 disassembler (disasm.c).
 * Covers all addressing modes and a selection of opcodes to verify
 * correct mnemonic output and instruction length return values.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "disasm.h"

static int test_pass = 0;
static int test_fail = 0;

#define ASSERT_STR(got, expected, msg)                                  \
    do {                                                                 \
        if (strcmp((got), (expected)) == 0) {                          \
            printf("  PASS: %s\n", msg);                               \
            test_pass++;                                                 \
        } else {                                                         \
            printf("  FAIL: %s\n    got:      \"%s\"\n    expected: \"%s\"  (line %d)\n", \
                   msg, (got), (expected), __LINE__);                   \
            test_fail++;                                                 \
        }                                                                \
    } while (0)

#define ASSERT_INT(got, expected, msg)                                  \
    do {                                                                 \
        if ((got) == (expected)) {                                      \
            printf("  PASS: %s\n", msg);                               \
            test_pass++;                                                 \
        } else {                                                         \
            printf("  FAIL: %s\n    got: %d  expected: %d  (line %d)\n", \
                   msg, (got), (expected), __LINE__);                   \
            test_fail++;                                                 \
        }                                                                \
    } while (0)

/* Convenience: disassemble bytes and check output + length. */
static void check(const char *test_name,
                  uint16_t addr, uint8_t b0, uint8_t b1, uint8_t b2,
                  int expected_len, const char *expected_text) {
    char out[64] = {0};
    uint8_t bytes[3] = {b0, b1, b2};
    int len = disasm_insn(addr, bytes, out, sizeof(out));
    ASSERT_INT(len, expected_len, test_name);
    ASSERT_STR(out, expected_text, test_name);
}

/* ---- Addressing mode tests ------------------------------------------- */

static void test_implied(void) {
    printf("test_implied\n");
    check("NOP IMP",    0x0000, 0xEA, 0x00, 0x00, 1, "NOP");
    check("CLC IMP",    0x0000, 0x18, 0x00, 0x00, 1, "CLC");
    check("RTS IMP",    0x0000, 0x60, 0x00, 0x00, 1, "RTS");
    check("SEI IMP",    0x0000, 0x78, 0x00, 0x00, 1, "SEI");
    check("TAX IMP",    0x0000, 0xAA, 0x00, 0x00, 1, "TAX");
    check("TXS IMP",    0x0000, 0x9A, 0x00, 0x00, 1, "TXS");
}

static void test_accumulator(void) {
    printf("test_accumulator\n");
    check("ASL ACC",    0x0000, 0x0A, 0x00, 0x00, 1, "ASL A");
    check("LSR ACC",    0x0000, 0x4A, 0x00, 0x00, 1, "LSR A");
    check("ROL ACC",    0x0000, 0x2A, 0x00, 0x00, 1, "ROL A");
    check("ROR ACC",    0x0000, 0x6A, 0x00, 0x00, 1, "ROR A");
}

static void test_immediate(void) {
    printf("test_immediate\n");
    check("LDA IMM $01",  0x0000, 0xA9, 0x01, 0x00, 2, "LDA #$01");
    check("LDA IMM $FF",  0x0000, 0xA9, 0xFF, 0x00, 2, "LDA #$FF");
    check("LDX IMM $42",  0x0000, 0xA2, 0x42, 0x00, 2, "LDX #$42");
    check("ORA IMM $00",  0x0000, 0x09, 0x00, 0x00, 2, "ORA #$00");
    check("CPX IMM $80",  0x0000, 0xE0, 0x80, 0x00, 2, "CPX #$80");
}

static void test_zero_page(void) {
    printf("test_zero_page\n");
    check("LDA ZPG $42",  0x0000, 0xA5, 0x42, 0x00, 2, "LDA $42");
    check("STA ZPG $00",  0x0000, 0x85, 0x00, 0x00, 2, "STA $00");
    check("BIT ZPG $24",  0x0000, 0x24, 0x24, 0x00, 2, "BIT $24");
    check("DEC ZPG $10",  0x0000, 0xC6, 0x10, 0x00, 2, "DEC $10");
}

static void test_zero_page_x(void) {
    printf("test_zero_page_x\n");
    check("LDA ZPX $10",  0x0000, 0xB5, 0x10, 0x00, 2, "LDA $10,X");
    check("STA ZPX $20",  0x0000, 0x95, 0x20, 0x00, 2, "STA $20,X");
    check("INC ZPX $FF",  0x0000, 0xF6, 0xFF, 0x00, 2, "INC $FF,X");
}

static void test_zero_page_y(void) {
    printf("test_zero_page_y\n");
    check("STX ZPY $10",  0x0000, 0x96, 0x10, 0x00, 2, "STX $10,Y");
    check("LDX ZPY $40",  0x0000, 0xB6, 0x40, 0x00, 2, "LDX $40,Y");
}

static void test_absolute(void) {
    printf("test_absolute\n");
    check("LDA ABS $1234",  0x0000, 0xAD, 0x34, 0x12, 3, "LDA $1234");
    check("STA ABS $2000",  0x0000, 0x8D, 0x00, 0x20, 3, "STA $2000");
    check("JMP ABS $C000",  0x0000, 0x4C, 0x00, 0xC0, 3, "JMP $C000");
    check("JSR ABS $FF00",  0x0000, 0x20, 0x00, 0xFF, 3, "JSR $FF00");
    check("INC ABS $0200",  0x0000, 0xEE, 0x00, 0x02, 3, "INC $0200");
}

static void test_absolute_x(void) {
    printf("test_absolute_x\n");
    check("LDA ABX $1234",  0x0000, 0xBD, 0x34, 0x12, 3, "LDA $1234,X");
    check("STA ABX $4400",  0x0000, 0x9D, 0x00, 0x44, 3, "STA $4400,X");
    check("ASL ABX $0200",  0x0000, 0x1E, 0x00, 0x02, 3, "ASL $0200,X");
}

static void test_absolute_y(void) {
    printf("test_absolute_y\n");
    check("LDA ABY $ABCD",  0x0000, 0xB9, 0xCD, 0xAB, 3, "LDA $ABCD,Y");
    check("STA ABY $0300",  0x0000, 0x99, 0x00, 0x03, 3, "STA $0300,Y");
    check("ORA ABY $1000",  0x0000, 0x19, 0x00, 0x10, 3, "ORA $1000,Y");
}

static void test_indirect(void) {
    printf("test_indirect\n");
    check("JMP IND $ABCD",  0x0000, 0x6C, 0xCD, 0xAB, 3, "JMP ($ABCD)");
    check("JMP IND $0300",  0x0000, 0x6C, 0x00, 0x03, 3, "JMP ($0300)");
}

static void test_indexed_indirect(void) {
    printf("test_indexed_indirect\n");
    check("LDA IDX ($10,X)",  0x0000, 0xA1, 0x10, 0x00, 2, "LDA ($10,X)");
    check("STA IDX ($00,X)",  0x0000, 0x81, 0x00, 0x00, 2, "STA ($00,X)");
    check("ORA IDX ($20,X)",  0x0000, 0x01, 0x20, 0x00, 2, "ORA ($20,X)");
}

static void test_indirect_indexed(void) {
    printf("test_indirect_indexed\n");
    check("LDA IDY ($10),Y",  0x0000, 0xB1, 0x10, 0x00, 2, "LDA ($10),Y");
    check("STA IDY ($00),Y",  0x0000, 0x91, 0x00, 0x00, 2, "STA ($00),Y");
    check("CMP IDY ($40),Y",  0x0000, 0xD1, 0x40, 0x00, 2, "CMP ($40),Y");
}

static void test_relative(void) {
    printf("test_relative\n");
    /* BNE at $C000 with offset +2 → target $C004 */
    check("BNE REL forward",   0xC000, 0xD0, 0x02, 0x00, 2, "BNE $C004");
    /* BNE at $C000 with offset -2 (0xFE) → target $C000 */
    check("BNE REL backward",  0xC000, 0xD0, 0xFE, 0x00, 2, "BNE $C000");
    /* BEQ at $C010 with offset -16 (0xF0) → target $C002 */
    check("BEQ REL backward2", 0xC010, 0xF0, 0xF0, 0x00, 2, "BEQ $C002");
    /* BPL at $0100 with offset +0x7F (127) → target $0181 (0x0100+2+127=0x0181) */
    check("BPL REL max fwd",   0x0100, 0x10, 0x7F, 0x00, 2, "BPL $0181");
    /* BMI at $0100 with offset -128 (0x80) → target $0082 */
    check("BMI REL max bwd",   0x0100, 0x30, 0x80, 0x00, 2, "BMI $0082");
}

/* ---- Spot-check illegal opcodes ---------------------------------------- */

static void test_illegal_opcodes(void) {
    printf("test_illegal_opcodes\n");
    check("SLO IDX $10",   0x0000, 0x03, 0x10, 0x00, 2, "SLO ($10,X)");
    check("RLA ZPG $42",   0x0000, 0x27, 0x42, 0x00, 2, "RLA $42");
    check("SRE ABS $1234", 0x0000, 0x4F, 0x34, 0x12, 3, "SRE $1234");
    check("RRA ABX $0300", 0x0000, 0x7F, 0x00, 0x03, 3, "RRA $0300,X");
    check("LAX ZPG $50",   0x0000, 0xA7, 0x50, 0x00, 2, "LAX $50");
    check("DCP ABS $4000", 0x0000, 0xCF, 0x00, 0x40, 3, "DCP $4000");
    check("ISB IDY ($20)", 0x0000, 0xF3, 0x20, 0x00, 2, "ISB ($20),Y");
    check("ANC IMM $FF",   0x0000, 0x0B, 0xFF, 0x00, 2, "ANC #$FF");
}

/* ---- Unknown opcodes --------------------------------------------------- */

static void test_unknown_opcodes(void) {
    printf("test_unknown_opcodes\n");
    check("??? $02",  0x0000, 0x02, 0x00, 0x00, 1, "???");
    check("??? $12",  0x0000, 0x12, 0x00, 0x00, 1, "???");
    check("??? $92",  0x0000, 0x92, 0x00, 0x00, 1, "???");
    check("??? $F2",  0x0000, 0xF2, 0x00, 0x00, 1, "???");
}

/* ---- Common game instruction sequences --------------------------------- */

static void test_sequences(void) {
    printf("test_sequences\n");
    /* LDA $00, TAX, BEQ target — a typical zero check */
    check("seq LDA ZPG",  0xC000, 0xA5, 0x00, 0x00, 2, "LDA $00");
    check("seq TAX",      0xC002, 0xAA, 0x00, 0x00, 1, "TAX");
    check("seq BEQ +5",   0xC003, 0xF0, 0x05, 0x00, 2, "BEQ $C00A");
}

int main(void) {
    printf("=== test_disasm ===\n\n");

    test_implied();
    test_accumulator();
    test_immediate();
    test_zero_page();
    test_zero_page_x();
    test_zero_page_y();
    test_absolute();
    test_absolute_x();
    test_absolute_y();
    test_indirect();
    test_indexed_indirect();
    test_indirect_indexed();
    test_relative();
    test_illegal_opcodes();
    test_unknown_opcodes();
    test_sequences();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return (test_fail > 0) ? 1 : 0;
}
