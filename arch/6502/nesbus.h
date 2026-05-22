#ifndef __NESBUS_H__
#define __NESBUS_H__

#include <stdint.h>

#include "2c02.h"
#include "6502.h"
#include "2a03.h"
#include "cartridge.h"
#include "controller.h"

struct nesbus;

typedef uint8_t (*fp_read)(uint16_t addr);
typedef void (*fp_write)(uint16_t addr, uint8_t data);

typedef uint8_t *(*fp_debug_read)(uint16_t offset, uint8_t *buf, uint16_t len);

/* Breakpoint hook: called after every CPU read (is_write=0) or write (is_write=1).
 * Set bp_hook to NULL to disable.  ud is forwarded as-is. */
typedef void (*nesbus_bp_hook_fn)(uint16_t addr, int is_write, void *ud);

struct nesbus {
    fp_read read;
    fp_write write;
    fp_clock clock;
    fp_connect_cartridge connect_cartridge;
    fp_debug_read debug_read;
    struct cpu6502 *cpu;
    struct ppu2c02 *ppu;
    struct nes_cartridge *cart;
    struct controller *controller1;
    struct controller *controller2;
    struct apu2a03 *apu;
    uint16_t dma_halt_cycles;
    uint64_t total_cycles;   /* incremented once per CPU cycle in emu_tick() */
    uint8_t  last_bus_value; /* open bus: last value returned by a real read */
    nesbus_bp_hook_fn bp_hook;
    void             *bp_hook_ud;
};

struct nesbus *nesbus_init(struct cpu6502 *cpu, struct ppu2c02 *ppu);

#define NES_RAM_SIZE 2048

/* Copy the 2 KB CPU RAM into/out of buf (for save states). */
void nesbus_get_ram(uint8_t *buf);
void nesbus_set_ram(const uint8_t *buf);

#endif /* __NESBUS_H__ */