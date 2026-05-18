#ifndef __DISASM_H__
#define __DISASM_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Disassemble one 6502 instruction.
 *
 * addr  - the address of the instruction (used to compute REL branch targets)
 * bytes - at least 3 bytes starting at addr (only as many as needed are read)
 * out   - output buffer filled with mnemonic + operand, e.g. "LDA #$01"
 * out_len - size of out buffer
 *
 * Returns: instruction length in bytes (1, 2, or 3).
 */
int disasm_insn(uint16_t addr, const uint8_t bytes[3], char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* __DISASM_H__ */
