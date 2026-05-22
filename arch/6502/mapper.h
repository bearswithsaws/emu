#ifndef __MAPPER_H__
#define __MAPPER_H__

#include <stdint.h>

#include "cartridge.h"

struct mapper;

typedef uint8_t (*fp_mapper_read)(struct mapper *map, uint16_t addr);
typedef void (*fp_mapper_write)(struct mapper *map, uint16_t addr,
                                uint8_t data);
// Called by the PPU once per scanline (at dot 260) for mappers that need
// a scanline-based IRQ counter (e.g. MMC3).  May be NULL for mappers that
// don't use it.
typedef void (*fp_mapper_scanline)(struct mapper *map);

// Mirroring modes used by the mapper (and read by the PPU).
// Mappers that support dynamic mirroring (e.g. MMC1) update this field;
// the PPU reads it instead of the static ROM header bit.
#define MIRROR_HORIZONTAL  0  // $2000=$2400, $2800=$2C00
#define MIRROR_VERTICAL    1  // $2000=$2800, $2400=$2C00
#define MIRROR_SINGLE_LO   2  // all nametables → first 1 KB
#define MIRROR_SINGLE_HI   3  // all nametables → second 1 KB

struct mapper {
    uint8_t mapper_id;
    fp_mapper_read cpu_read;
    fp_mapper_write cpu_write;
    fp_mapper_read ppu_read;
    fp_mapper_write ppu_write;
    struct nes_cartridge *cartridge;
    uint8_t num_prg_rom;
    uint8_t num_chr_rom;
    uint8_t mirroring;          // one of MIRROR_* above; updated dynamically by mapper
    uint8_t irq_pending;        // set by mapper to request a CPU IRQ; cleared by cpu.irq()
    fp_mapper_scanline scanline; // PPU scanline hook — NULL if unused
    fp_mapper_scanline clock;   // per-CPU-cycle clock hook — NULL if unused
    void    *ctx;               // per-mapper private state (allocated by mapper init)
    uint32_t ctx_size;          // sizeof(*ctx), set by mapper init; 0 if no ctx
    uint8_t *prg_ram;           // pointer into ctx for battery-backed PRG-RAM; NULL if none
    uint32_t prg_ram_size;      // size in bytes of prg_ram; 0 if none
};

struct mapper *mapper_init(struct nes_cartridge *cartridge);

#endif /* __MAPPER_H__ */