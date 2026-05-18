/*
 * savestate.c — NES emulator save/load state
 *
 * Binary format (sequential, no padding between sections):
 *
 *   [save_header_t]         fixed-size file header with magic, version, mapper info
 *   [cpu_snap_t]            CPU registers and execution state
 *   [ppu_snap_t]            PPU registers, VRAM, OAM, palette, rendering state
 *   [uint8_t ram[2048]]     CPU RAM
 *   [apu_snap_t]            APU channel state (ring buffer excluded)
 *   [ctrl_snap_t × 2]       controller shift registers
 *   [uint8_t mirroring]     mapper dynamic mirroring
 *   [uint8_t irq_pending]   mapper IRQ flag
 *   [uint8_t ctx[ctx_size]] mapper private context blob (ctx_size from header)
 *   [uint8_t chr[chr_size]] CHR-RAM bytes (chr_size from header; 0 if CHR-ROM)
 */

#include "savestate.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "6502.h"
#include "2c02.h"
#include "2a03.h"
#include "cartridge.h"
#include "mapper.h"
#include "nesbus.h"

/* -------------------------------------------------------------------------
 * File format constants
 * ---------------------------------------------------------------------- */

#define SAVESTATE_MAGIC   0x454D5353u  /* "EMSS" */
#define SAVESTATE_VERSION 1u

/* -------------------------------------------------------------------------
 * Snapshot structs — only serialisable fields, no pointers
 * ---------------------------------------------------------------------- */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t  mapper_id;
    uint8_t  has_chr_ram;   /* 1 if cartridge uses CHR-RAM */
    uint32_t chr_ram_size;  /* bytes in CHR-RAM blob (0 if ROM) */
    uint32_t ctx_size;      /* bytes of mapper ctx blob */
} save_header_t;

typedef struct {
    uint8_t  flags;         /* P register as a byte */
    uint8_t  A, Y, X;
    uint16_t PC;
    uint8_t  sp;
    uint8_t  opcode;
    uint8_t  operand;
    uint16_t operand_addr;
    uint8_t  cycles;
} cpu_snap_t;

typedef struct {
    /* VRAM and palette */
    uint8_t  nametable[0x2000];
    uint8_t  palette_table[0x20];
    /* Registers */
    uint8_t  ppuctrl;
    uint8_t  ppumask;
    uint8_t  ppustatus;
    uint8_t  oamaddr;
    uint8_t  oamdata;
    uint16_t ppuaddr;
    uint8_t  ppuaddr_latch;
    uint8_t  ppuscroll;
    uint8_t  ppudata;
    /* Loopy scroll */
    uint16_t v, t;
    uint8_t  x, w;
    /* OAM */
    uint8_t  oam[256];
    struct { uint8_t y, tile, attr, x, is_sprite_zero; } secondary_oam[8];
    uint8_t  sprite_count;
    /* Background rendering shift registers / latches */
    uint16_t bg_shift_pattern_lo;
    uint16_t bg_shift_pattern_hi;
    uint8_t  bg_shift_attr_lo;
    uint8_t  bg_shift_attr_hi;
    uint8_t  bg_attr_latch_lo;
    uint8_t  bg_attr_latch_hi;
    uint8_t  bg_next_tile_id;
    uint8_t  bg_next_tile_attr;
    uint8_t  bg_next_tile_lsb;
    uint8_t  bg_next_tile_msb;
    /* Timing */
    int16_t  scanline;
    int16_t  dot;
    uint8_t  frame_complete;
    uint8_t  nmi_triggered;
    uint8_t  ppudata_read_buffer;
} ppu_snap_t;

/* APU: everything except ring buffer, atomic indices, cpu_read pointer,
 * and cycles_per_sample (re-calibrated from SDL on load). */
typedef struct {
    struct apu_pulse         pulse1;
    struct apu_pulse         pulse2;
    struct apu_triangle      triangle;
    struct apu_noise         noise;
    struct apu_dmc           dmc;
    struct apu_frame_counter frame;
    float  sample_accum;
    float  sample_cycles;
    float  hp1_prev_in,  hp1_prev_out;
    float  hp2_prev_in,  hp2_prev_out;
    float  lp_prev;
    int    pulse_clock_toggle;
    int    irq_pending;  /* saved as int to avoid _Bool ABI concerns */
} apu_snap_t;

typedef struct {
    uint8_t buttons;
    uint8_t shift_reg;
    uint8_t strobe;
} ctrl_snap_t;

/* -------------------------------------------------------------------------
 * Save-file path helper
 * ---------------------------------------------------------------------- */

char *savestate_path(int slot, char *buf, size_t len) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (home) {
        /* Ensure directory exists */
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/.local/share/emu", home);
#ifdef _WIN32
        _mkdir(dir);
#else
        mkdir(dir, 0755);
#endif
        snprintf(buf, len, "%s/slot_%d.sav", dir, slot);
    } else {
        snprintf(buf, len, "slot_%d.sav", slot);
    }
    return buf;
}

/* -------------------------------------------------------------------------
 * Save
 * ---------------------------------------------------------------------- */

int savestate_save(int slot, struct nesbus *bus) {
    if (!bus || !bus->cpu || !bus->ppu || !bus->cart) {
        fprintf(stderr, "savestate_save: no ROM loaded\n");
        return -1;
    }

    char path[512];
    savestate_path(slot, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "savestate_save: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct nes_cartridge *cart = bus->cart;
    struct mapper        *map  = cart->map;

    /* ---- header ---- */
    save_header_t hdr = {
        .magic        = SAVESTATE_MAGIC,
        .version      = SAVESTATE_VERSION,
        .mapper_id    = cart->mapper_id,
        .has_chr_ram  = cart->chr_ram_allocated,
        .chr_ram_size = cart->chr_ram_allocated ? cart->chr_rom_len : 0,
        .ctx_size     = map ? map->ctx_size : 0,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);

    /* ---- CPU ---- */
    struct cpu6502 *cpu = bus->cpu;
    cpu_snap_t cs = {
        .flags       = cpu->flags.reg,
        .A           = cpu->A,
        .Y           = cpu->Y,
        .X           = cpu->X,
        .PC          = cpu->PC,
        .sp          = cpu->sp,
        .opcode      = cpu->opcode,
        .operand     = cpu->operand,
        .operand_addr = cpu->operand_addr,
        .cycles      = cpu->cycles,
    };
    fwrite(&cs, sizeof(cs), 1, f);

    /* ---- PPU ---- */
    struct ppu2c02 *ppu = bus->ppu;
    ppu_snap_t ps;
    memset(&ps, 0, sizeof(ps));
    memcpy(ps.nametable,    ppu->nametable,    sizeof(ps.nametable));
    memcpy(ps.palette_table, ppu->palette_table, sizeof(ps.palette_table));
    ps.ppuctrl         = ppu->ppuctrl.reg;
    ps.ppumask         = ppu->ppumask.reg;
    ps.ppustatus       = ppu->ppustatus.reg;
    ps.oamaddr         = ppu->oamaddr;
    ps.oamdata         = ppu->oamdata;
    ps.ppuaddr         = ppu->ppuaddr;
    ps.ppuaddr_latch   = ppu->ppuaddr_latch;
    ps.ppuscroll       = ppu->ppuscroll;
    ps.ppudata         = ppu->ppudata;
    ps.v               = ppu->v;
    ps.t               = ppu->t;
    ps.x               = ppu->x;
    ps.w               = ppu->w;
    memcpy(ps.oam, ppu->oam, sizeof(ps.oam));
    for (int i = 0; i < 8; i++) {
        ps.secondary_oam[i].y              = ppu->secondary_oam[i].y;
        ps.secondary_oam[i].tile           = ppu->secondary_oam[i].tile;
        ps.secondary_oam[i].attr           = ppu->secondary_oam[i].attr;
        ps.secondary_oam[i].x              = ppu->secondary_oam[i].x;
        ps.secondary_oam[i].is_sprite_zero = ppu->secondary_oam[i].is_sprite_zero;
    }
    ps.sprite_count         = ppu->sprite_count;
    ps.bg_shift_pattern_lo  = ppu->bg_shift_pattern_lo;
    ps.bg_shift_pattern_hi  = ppu->bg_shift_pattern_hi;
    ps.bg_shift_attr_lo     = ppu->bg_shift_attr_lo;
    ps.bg_shift_attr_hi     = ppu->bg_shift_attr_hi;
    ps.bg_attr_latch_lo     = ppu->bg_attr_latch_lo;
    ps.bg_attr_latch_hi     = ppu->bg_attr_latch_hi;
    ps.bg_next_tile_id      = ppu->bg_next_tile_id;
    ps.bg_next_tile_attr    = ppu->bg_next_tile_attr;
    ps.bg_next_tile_lsb     = ppu->bg_next_tile_lsb;
    ps.bg_next_tile_msb     = ppu->bg_next_tile_msb;
    ps.scanline             = ppu->scanline;
    ps.dot                  = ppu->dot;
    ps.frame_complete       = ppu->frame_complete;
    ps.nmi_triggered        = ppu->nmi_triggered;
    ps.ppudata_read_buffer  = ppu->ppudata_read_buffer;
    fwrite(&ps, sizeof(ps), 1, f);

    /* ---- CPU RAM ---- */
    uint8_t ram[NES_RAM_SIZE];
    nesbus_get_ram(ram);
    fwrite(ram, NES_RAM_SIZE, 1, f);

    /* ---- APU ---- */
    struct apu2a03 *apu = bus->apu;
    apu_snap_t as;
    memset(&as, 0, sizeof(as));
    as.pulse1             = apu->pulse1;
    as.pulse2             = apu->pulse2;
    as.triangle           = apu->triangle;
    as.noise              = apu->noise;
    as.dmc                = apu->dmc;
    as.frame              = apu->frame;
    as.sample_accum       = apu->sample_accum;
    as.sample_cycles      = apu->sample_cycles;
    as.hp1_prev_in        = apu->hp1_prev_in;
    as.hp1_prev_out       = apu->hp1_prev_out;
    as.hp2_prev_in        = apu->hp2_prev_in;
    as.hp2_prev_out       = apu->hp2_prev_out;
    as.lp_prev            = apu->lp_prev;
    as.pulse_clock_toggle = apu->pulse_clock_toggle;
    as.irq_pending        = (int)apu->irq_pending;
    fwrite(&as, sizeof(as), 1, f);

    /* ---- controllers ---- */
    ctrl_snap_t ctrl[2] = {
        { bus->controller1->buttons, bus->controller1->shift_reg, bus->controller1->strobe },
        { bus->controller2->buttons, bus->controller2->shift_reg, bus->controller2->strobe },
    };
    fwrite(ctrl, sizeof(ctrl), 1, f);

    /* ---- mapper base state ---- */
    if (map) {
        fwrite(&map->mirroring,   1, 1, f);
        fwrite(&map->irq_pending, 1, 1, f);
        if (map->ctx && map->ctx_size > 0)
            fwrite(map->ctx, map->ctx_size, 1, f);
    } else {
        uint8_t zero[2] = {0, 0};
        fwrite(zero, 2, 1, f);
    }

    /* ---- CHR-RAM ---- */
    if (cart->chr_ram_allocated && cart->chr_rom && cart->chr_rom_len > 0)
        fwrite(cart->chr_rom, cart->chr_rom_len, 1, f);

    fclose(f);
    printf("State saved to slot %d: %s\n", slot, path);
    return 0;
}

/* -------------------------------------------------------------------------
 * Load
 * ---------------------------------------------------------------------- */

int savestate_load(int slot, struct nesbus *bus) {
    if (!bus || !bus->cpu || !bus->ppu || !bus->cart) {
        fprintf(stderr, "savestate_load: no ROM loaded\n");
        return -1;
    }

    char path[512];
    savestate_path(slot, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "savestate_load: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct nes_cartridge *cart = bus->cart;
    struct mapper        *map  = cart->map;

    /* ---- header ---- */
    save_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) goto bad;
    if (hdr.magic != SAVESTATE_MAGIC) {
        fprintf(stderr, "savestate_load: bad magic in %s\n", path);
        goto bad;
    }
    if (hdr.version != SAVESTATE_VERSION) {
        fprintf(stderr, "savestate_load: unsupported version %u\n", hdr.version);
        goto bad;
    }
    if (hdr.mapper_id != cart->mapper_id) {
        fprintf(stderr, "savestate_load: mapper mismatch (save=%u, rom=%u)\n",
                hdr.mapper_id, cart->mapper_id);
        goto bad;
    }

    /* ---- CPU ---- */
    struct cpu6502 *cpu = bus->cpu;
    cpu_snap_t cs;
    if (fread(&cs, sizeof(cs), 1, f) != 1) goto bad;
    cpu->flags.reg    = cs.flags;
    cpu->A            = cs.A;
    cpu->Y            = cs.Y;
    cpu->X            = cs.X;
    cpu->PC           = cs.PC;
    cpu->sp           = cs.sp;
    cpu->opcode       = cs.opcode;
    cpu->operand      = cs.operand;
    cpu->operand_addr = cs.operand_addr;
    cpu->cycles       = cs.cycles;

    /* ---- PPU ---- */
    struct ppu2c02 *ppu = bus->ppu;
    ppu_snap_t ps;
    if (fread(&ps, sizeof(ps), 1, f) != 1) goto bad;
    memcpy(ppu->nametable,     ps.nametable,    sizeof(ps.nametable));
    memcpy(ppu->palette_table, ps.palette_table, sizeof(ps.palette_table));
    ppu->ppuctrl.reg       = ps.ppuctrl;
    ppu->ppumask.reg       = ps.ppumask;
    ppu->ppustatus.reg     = ps.ppustatus;
    ppu->oamaddr           = ps.oamaddr;
    ppu->oamdata           = ps.oamdata;
    ppu->ppuaddr           = ps.ppuaddr;
    ppu->ppuaddr_latch     = ps.ppuaddr_latch;
    ppu->ppuscroll         = ps.ppuscroll;
    ppu->ppudata           = ps.ppudata;
    ppu->v                 = ps.v;
    ppu->t                 = ps.t;
    ppu->x                 = ps.x;
    ppu->w                 = ps.w;
    memcpy(ppu->oam, ps.oam, sizeof(ps.oam));
    for (int i = 0; i < 8; i++) {
        ppu->secondary_oam[i].y              = ps.secondary_oam[i].y;
        ppu->secondary_oam[i].tile           = ps.secondary_oam[i].tile;
        ppu->secondary_oam[i].attr           = ps.secondary_oam[i].attr;
        ppu->secondary_oam[i].x              = ps.secondary_oam[i].x;
        ppu->secondary_oam[i].is_sprite_zero = ps.secondary_oam[i].is_sprite_zero;
    }
    ppu->sprite_count        = ps.sprite_count;
    ppu->bg_shift_pattern_lo = ps.bg_shift_pattern_lo;
    ppu->bg_shift_pattern_hi = ps.bg_shift_pattern_hi;
    ppu->bg_shift_attr_lo    = ps.bg_shift_attr_lo;
    ppu->bg_shift_attr_hi    = ps.bg_shift_attr_hi;
    ppu->bg_attr_latch_lo    = ps.bg_attr_latch_lo;
    ppu->bg_attr_latch_hi    = ps.bg_attr_latch_hi;
    ppu->bg_next_tile_id     = ps.bg_next_tile_id;
    ppu->bg_next_tile_attr   = ps.bg_next_tile_attr;
    ppu->bg_next_tile_lsb    = ps.bg_next_tile_lsb;
    ppu->bg_next_tile_msb    = ps.bg_next_tile_msb;
    ppu->scanline            = ps.scanline;
    ppu->dot                 = ps.dot;
    ppu->frame_complete      = ps.frame_complete;
    ppu->nmi_triggered       = ps.nmi_triggered;
    ppu->ppudata_read_buffer = ps.ppudata_read_buffer;

    /* ---- CPU RAM ---- */
    uint8_t ram[NES_RAM_SIZE];
    if (fread(ram, NES_RAM_SIZE, 1, f) != 1) goto bad;
    nesbus_set_ram(ram);

    /* ---- APU ---- */
    struct apu2a03 *apu = bus->apu;
    apu_snap_t as;
    if (fread(&as, sizeof(as), 1, f) != 1) goto bad;
    /* Preserve runtime-only fields: cpu_read callback and cycles_per_sample */
    uint8_t (*saved_cpu_read)(uint16_t) = apu->cpu_read;
    float    saved_cps                  = apu->cycles_per_sample;
    apu->pulse1             = as.pulse1;
    apu->pulse2             = as.pulse2;
    apu->triangle           = as.triangle;
    apu->noise              = as.noise;
    apu->dmc                = as.dmc;
    apu->frame              = as.frame;
    apu->sample_accum       = as.sample_accum;
    apu->sample_cycles      = as.sample_cycles;
    apu->hp1_prev_in        = as.hp1_prev_in;
    apu->hp1_prev_out       = as.hp1_prev_out;
    apu->hp2_prev_in        = as.hp2_prev_in;
    apu->hp2_prev_out       = as.hp2_prev_out;
    apu->lp_prev            = as.lp_prev;
    apu->pulse_clock_toggle = as.pulse_clock_toggle;
    apu->irq_pending        = (bool)as.irq_pending;
    apu->cpu_read           = saved_cpu_read;
    apu->cycles_per_sample  = saved_cps;
    /* Reset audio ring so stale samples don't cause a pop */
    apu_ring_reset(apu);

    /* ---- controllers ---- */
    ctrl_snap_t ctrl[2];
    if (fread(ctrl, sizeof(ctrl), 1, f) != 1) goto bad;
    bus->controller1->buttons   = ctrl[0].buttons;
    bus->controller1->shift_reg = ctrl[0].shift_reg;
    bus->controller1->strobe    = ctrl[0].strobe;
    bus->controller2->buttons   = ctrl[1].buttons;
    bus->controller2->shift_reg = ctrl[1].shift_reg;
    bus->controller2->strobe    = ctrl[1].strobe;

    /* ---- mapper base state ---- */
    if (map) {
        uint8_t mirroring, irq;
        if (fread(&mirroring, 1, 1, f) != 1) goto bad;
        if (fread(&irq,       1, 1, f) != 1) goto bad;
        map->mirroring   = mirroring;
        map->irq_pending = irq;
        if (map->ctx && map->ctx_size > 0 && hdr.ctx_size == map->ctx_size) {
            if (fread(map->ctx, map->ctx_size, 1, f) != 1) goto bad;
        } else if (hdr.ctx_size > 0) {
            /* ctx_size mismatch — skip blob to stay in sync */
            fseek(f, (long)hdr.ctx_size, SEEK_CUR);
        }
    } else {
        fseek(f, 2, SEEK_CUR);
    }

    /* ---- CHR-RAM ---- */
    if (hdr.has_chr_ram && hdr.chr_ram_size > 0) {
        if (cart->chr_ram_allocated && cart->chr_rom &&
            cart->chr_rom_len == hdr.chr_ram_size) {
            if (fread(cart->chr_rom, hdr.chr_ram_size, 1, f) != 1) goto bad;
        } else {
            fseek(f, (long)hdr.chr_ram_size, SEEK_CUR);
        }
    }

    fclose(f);
    printf("State loaded from slot %d: %s\n", slot, path);
    return 0;

bad:
    fclose(f);
    fprintf(stderr, "savestate_load: read error or unexpected EOF in %s\n", path);
    return -1;
}
