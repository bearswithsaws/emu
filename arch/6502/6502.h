#ifndef __6502_H__
#define __6502_H__

#include "nesbus.h"
#include <stdint.h>

struct cpu6502;

typedef void (*fp_nmi)(void);
typedef void (*fp_irq)(void);
typedef void (*fp_reset)(void);
typedef uint8_t (*fp_read)(uint16_t addr);
typedef void (*fp_write)(uint16_t addr, uint8_t data);
typedef uint8_t (*fp_fetch)(void);
typedef uint8_t (*fp_execute)(void);
typedef void (*fp_print_regs)(void);

typedef void (*fp_clock)(void);
typedef void (*fp_connect_bus)(void *bus);

typedef uint8_t (*fp_addr_mode)(void);
typedef uint8_t (*fp_mnem)(void);

struct instruction {
    char *mnem;
    uint8_t opcode;
    fp_mnem execute;
    fp_addr_mode addr_mode;
    uint8_t cycles;
};

struct cpu6502 {
    fp_nmi nmi;
    fp_irq irq;
    fp_reset reset;
    fp_read read;
    fp_write write;
    fp_fetch fetch;
    fp_execute execute;
    fp_clock clock;
    fp_connect_bus connect_bus;
    fp_print_regs print_regs;

    struct nesbus *bus;

    union {
        struct {
            uint8_t C : 1;
            uint8_t Z : 1;
            uint8_t I : 1;
            uint8_t D : 1;
            uint8_t B : 1;
            uint8_t U : 1;
            uint8_t V : 1;
            uint8_t N : 1;
        };
        uint8_t reg;
    } flags;
    uint8_t A;
    uint8_t Y;
    uint8_t X;
    uint16_t PC;
    uint8_t sp;

    // DEBUG purposes
    uint16_t start_pc;

    uint8_t opcode;
    struct instruction *curr_insn;
    uint8_t operand;
    uint16_t operand_addr;
    uint8_t cycles;
};

#define SP(x) ((x.sp + 0x100))
#define SET_SP(x, p) ((x.sp = (p & 0xFF)))
#define INC_SP(x) ((x.sp++ ))
#define DEC_SP(x) ((x.sp-- ))


#define DECODE_A(inst) ((inst >> 5) & 0x7)
#define DECODE_B(inst) ((inst >> 2) & 0x7)
#define DECODE_C(inst) ((inst)&0x3)

#define GET_FLAG(f) (cpu.flags.f)
#define SET_FLAG(f, v) (cpu.flags.f = !!(v))

struct cpu6502 *cpu6502_init();

#endif /* __6502_H__ */
