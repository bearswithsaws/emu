#ifndef __2C02_H__
#define __2C02_H__

#include "cartridge.h"
#include "nesbus.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PPUCTRL 0x2000
#define PPUMASK 0x2001
#define PPUSTATUS 0x2002
#define OAMADDR 0x2003
#define OAMDATA 0x2004
#define PPUSCROLL 0x2005
#define PPUADDR 0x2006
#define PPUDATA 0x2007
#define SPRDMA 0x4014

struct ppu2c02;

typedef uint8_t (*fp_ppu_read)(uint16_t addr);
typedef void (*fp_ppu_write)(uint16_t addr, uint8_t data);
typedef uint8_t (*fp_cpu_read)(uint16_t addr);
typedef void (*fp_cpu_write)(uint16_t addr, uint8_t data);
typedef void (*fp_reset)(void);

typedef void (*fp_clock)(void);
typedef void (*fp_connect_bus)(void *bus);
typedef void (*fp_set_framebuffer)(uint32_t *fb);

struct ppu2c02 {
    fp_ppu_read ppu_read;
    fp_ppu_write ppu_write;
    fp_cpu_read cpu_read;
    fp_cpu_write cpu_write;
    fp_clock clock;
    fp_connect_bus connect_bus;
    fp_connect_cartridge connect_cartridge;
    fp_set_framebuffer set_framebuffer;
    fp_reset reset;
    struct nes_cartridge *cart;
    struct nesbus *bus;

    uint8_t pattern_table[0x2000]; // CHR ROM
    uint8_t nametable[0x2000];     // VRAM
    uint8_t palette_table[0x20];

    union {
        struct {
            uint8_t base_nametable_addr : 2;
            uint8_t vram_addr_increment : 1;
            uint8_t sprite_pattern_table : 1;
            uint8_t bg_pattern_table : 1;
            uint8_t sprite_size : 1;
            uint8_t ppu_select : 1;
            uint8_t nmi : 1;
        };
        uint8_t reg;
    } ppuctrl;

    union {
        struct {
            uint8_t grayscale : 1;
            uint8_t bg_enable : 1;
            uint8_t sprite_enable : 1;
            uint8_t bg_render_enable : 1;
            uint8_t sprite_render_enable : 1;
            uint8_t intensify_red : 1;
            uint8_t intensify_green : 1;
            uint8_t intensify_blue : 1;
        };
        uint8_t reg;
    } ppumask;

    union {
        struct {
            uint8_t unused : 5;
            uint8_t sprite_overflow : 1;
            uint8_t sprite_0_hit : 1;
            uint8_t vblank_started : 1;
        };
        uint8_t reg;
    } ppustatus;

    uint8_t oamaddr;    // $2003 - OAM address register
    uint8_t oamdata;    // $2004 - OAM data register (not used, actual data in oam[])
    uint8_t ppuscroll;  // Legacy field, will be removed
    uint16_t ppuaddr;   // Legacy field, will be removed
    uint8_t ppudata;

    uint8_t ppuaddr_latch;  // Legacy latch, replaced by 'w'

    // Loopy registers for scrolling (NESdev PPU scrolling)
    uint16_t v;  // Current VRAM address (15 bits)
    uint16_t t;  // Temporary VRAM address (15 bits)
    uint8_t x;   // Fine X scroll (3 bits: 0-7)
    uint8_t w;   // Write latch (1 bit: 0 or 1) - shared by PPUSCROLL and PPUADDR

    // OAM (Object Attribute Memory) - 256 bytes for 64 sprites
    // Each sprite: 4 bytes (Y, tile index, attributes, X)
    uint8_t oam[256];

    // Secondary OAM - holds up to 8 sprites for current scanline
    struct {
        uint8_t y;
        uint8_t tile;
        uint8_t attr;
        uint8_t x;
    } secondary_oam[8];
    uint8_t sprite_count;  // Number of sprites on current scanline (0-8)

    // Frame buffer for rendering output (provided by GUI, 256x240 ARGB8888)
    uint32_t *frame_buffer;

    // Background rendering shift registers (internal PPU registers)
    uint16_t bg_shift_pattern_lo;   // Low bit plane shift register (16-bit)
    uint16_t bg_shift_pattern_hi;   // High bit plane shift register (16-bit)
    uint8_t bg_shift_attr_lo;        // Low attribute shift register (8-bit)
    uint8_t bg_shift_attr_hi;        // High attribute shift register (8-bit)

    // Attribute latches (feed shift registers on every shift)
    uint8_t bg_attr_latch_lo;        // Low attribute latch (feeds bit 0 during shifts)
    uint8_t bg_attr_latch_hi;        // High attribute latch (feeds bit 0 during shifts)

    // Latches for next tile data (loaded into shift registers every 8 dots)
    uint8_t bg_next_tile_id;         // Next tile index from nametable
    uint8_t bg_next_tile_attr;       // Next tile attribute
    uint8_t bg_next_tile_lsb;        // Next tile pattern low byte
    uint8_t bg_next_tile_msb;        // Next tile pattern high byte

    // Scanline and dot counters for PPU timing
    int16_t scanline;  // -1 to 260 (NTSC: 262 scanlines total, -1 is pre-render)
    int16_t dot;       // 0 to 340 (341 dots per scanline)
    uint8_t frame_complete;  // Flag set when frame rendering is done

    // NMI signal (set by PPU, read by CPU via nesbus)
    uint8_t nmi_triggered;  // 1 when NMI should fire, cleared when CPU reads it

    // PPUDATA read buffer (internal buffering for reads from $0000-$3EFF)
    // Reads from $3F00-$3FFF (palette) bypass the buffer
    uint8_t ppudata_read_buffer;
};

struct ppu2c02 *ppu2c02_init();

#endif /* __2C02_H__ */