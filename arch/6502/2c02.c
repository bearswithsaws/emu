#include "2c02.h"

#include "debug.h"
#include "palette.h"

static struct ppu2c02 ppu = {0};

// ---------------------------------------------------------------------------
// Internal memory access
// ---------------------------------------------------------------------------

static void connect_cartridge(struct nes_cartridge *cartridge) {
    ppu.cart = cartridge;
}

static uint16_t nametable_mirror(uint16_t addr) {
    if (!ppu.cart || !ppu.cart->hdr) {
        return (addr - 0x2000) % 0x800;
    }

    // Normalise to $2000-$2FFF
    addr = 0x2000 + ((addr - 0x2000) % 0x1000);
    uint8_t nt = (addr >> 10) & 0x03;

    if (ppu.cart->hdr->flags6.mirroring == 0) {
        // Horizontal mirroring: $2000=$2400, $2800=$2C00
        // Nametables 0&1 share first 1 KB; 2&3 share second 1 KB.
        return (nt <= 1) ? (addr & 0x03FF) : (0x0400 | (addr & 0x03FF));
    } else {
        // Vertical mirroring: $2000=$2800, $2400=$2C00
        // Nametables 0&2 share first 1 KB; 1&3 share second 1 KB.
        return (nt == 0 || nt == 2) ? (addr & 0x03FF) : (0x0400 | (addr & 0x03FF));
    }
}

static uint8_t ppu_read(uint16_t addr) {
    uint8_t data = 0;

    if (addr < 0x2000) {
        if (ppu.cart && ppu.cart->ppu_read) {
            data = ppu.cart->ppu_read(ppu.cart, addr);
        }
    } else if (addr <= 0x3EFF) {
        data = ppu.nametable[nametable_mirror(addr)];
    } else if (addr <= 0x3FFF) {
        // Palette mirrors: $3F10/$3F14/$3F18/$3F1C mirror $3F00/$3F04/$3F08/$3F0C
        uint8_t pal_addr = addr & 0x1F;
        if (pal_addr == 0x10 || pal_addr == 0x14 || pal_addr == 0x18 || pal_addr == 0x1C) {
            pal_addr &= 0x0F;
        }
        data = ppu.palette_table[pal_addr];
    } else {
        data = ppu_read(addr & 0x3FFF);
    }

    return data;
}

static void ppu_write(uint16_t addr, uint8_t data) {
    if (addr < 0x2000) {
        if (ppu.cart && ppu.cart->ppu_write) {
            ppu.cart->ppu_write(ppu.cart, addr, data);
        }
    } else if (addr <= 0x3EFF) {
        ppu.nametable[nametable_mirror(addr)] = data;
    } else if (addr <= 0x3FFF) {
        uint8_t pal_addr = addr & 0x1F;
        if (pal_addr == 0x10 || pal_addr == 0x14 || pal_addr == 0x18 || pal_addr == 0x1C) {
            pal_addr &= 0x0F;
        }
        ppu.palette_table[pal_addr] = data;
    } else {
        ppu_write(addr & 0x3FFF, data);
    }
}

// ---------------------------------------------------------------------------
// CPU register interface ($2000-$2007)
// ---------------------------------------------------------------------------

static uint8_t cpu_read(uint16_t addr) {
    uint8_t data = 0;

    switch (addr & 0x0007) {
    case 0x0002: // PPUSTATUS
        data = (ppu.ppustatus.reg & 0xE0) | (ppu.ppudata_read_buffer & 0x1F);
        ppu.ppustatus.vblank_started = 0;
        ppu.w = 0;
        break;

    case 0x0004: // OAMDATA
        data = ppu.oam[ppu.oamaddr];
        break;

    case 0x0007: // PPUDATA
    {
        uint16_t v_addr = ppu.v & 0x3FFF;
        if (v_addr >= 0x3F00) {
            // Palette: immediate read, buffer gets the nametable "under" it
            uint8_t pal_addr = v_addr & 0x1F;
            if (pal_addr == 0x10 || pal_addr == 0x14 ||
                pal_addr == 0x18 || pal_addr == 0x1C) {
                pal_addr &= 0x0F;
            }
            data = ppu.palette_table[pal_addr];
            ppu.ppudata_read_buffer = ppu_read(v_addr & 0x2FFF);
        } else {
            data = ppu.ppudata_read_buffer;
            ppu.ppudata_read_buffer = ppu_read(v_addr);
        }
        ppu.v += ppu.ppuctrl.vram_addr_increment ? 32 : 1;
        break;
    }

    default:
        break;
    }

    return data;
}

static void cpu_write(uint16_t addr, uint8_t data) {
    switch (addr & 0x0007) {
    case 0x0000: // PPUCTRL
        ppu.ppuctrl.reg = data;
        // Nametable select goes into t bits 11-10
        ppu.t = (ppu.t & 0xF3FF) | ((uint16_t)(data & 0x03) << 10);
        break;

    case 0x0001: // PPUMASK
        ppu.ppumask.reg = data;
        break;

    case 0x0003: // OAMADDR
        ppu.oamaddr = data;
        break;

    case 0x0004: // OAMDATA
        ppu.oam[ppu.oamaddr++] = data;
        break;

    case 0x0005: // PPUSCROLL
        if (ppu.w == 0) {
            ppu.t = (ppu.t & 0xFFE0) | (data >> 3);
            ppu.x = data & 0x07;
            ppu.w = 1;
        } else {
            ppu.t = (ppu.t & 0x8FFF) | ((uint16_t)(data & 0x07) << 12);
            ppu.t = (ppu.t & 0xFC1F) | ((uint16_t)(data & 0xF8) << 2);
            ppu.w = 0;
        }
        break;

    case 0x0006: // PPUADDR
        if (ppu.w == 0) {
            ppu.t = (ppu.t & 0x00FF) | ((uint16_t)(data & 0x3F) << 8);
            ppu.w = 1;
        } else {
            ppu.t = (ppu.t & 0xFF00) | data;
            ppu.v = ppu.t;
            ppu.w = 0;
        }
        break;

    case 0x0007: // PPUDATA
        ppu_write(ppu.v & 0x3FFF, data);
        ppu.v += ppu.ppuctrl.vram_addr_increment ? 32 : 1;
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static int rendering_enabled(void) {
    return ppu.ppumask.bg_render_enable || ppu.ppumask.sprite_render_enable;
}

// ---------------------------------------------------------------------------
// Background tile fetch pipeline
// Called at specific dots within each 8-dot sub-cycle.
// ---------------------------------------------------------------------------

static void fetch_nametable_byte(void) {
    uint16_t addr = 0x2000 | (ppu.v & 0x0FFF);
    ppu.bg_next_tile_id = ppu.nametable[nametable_mirror(addr)];
}

static void fetch_attribute_byte(void) {
    uint16_t addr = 0x23C0
                  | (ppu.v & 0x0C00)
                  | ((ppu.v >> 4) & 0x38)
                  | ((ppu.v >> 2) & 0x07);
    uint8_t attr = ppu.nametable[nametable_mirror(addr)];
    uint8_t shift = ((ppu.v >> 4) & 0x04) | (ppu.v & 0x02);
    ppu.bg_next_tile_attr = (attr >> shift) & 0x03;
}

static void fetch_pattern_low_byte(void) {
    uint16_t base = ppu.ppuctrl.bg_pattern_table ? 0x1000 : 0x0000;
    uint16_t fine_y = (ppu.v >> 12) & 0x07;
    uint16_t addr = base + ((uint16_t)ppu.bg_next_tile_id << 4) + fine_y;
    ppu.bg_next_tile_lsb = (ppu.cart && ppu.cart->ppu_read)
                               ? ppu.cart->ppu_read(ppu.cart, addr)
                               : 0;
}

static void fetch_pattern_high_byte(void) {
    uint16_t base = ppu.ppuctrl.bg_pattern_table ? 0x1000 : 0x0000;
    uint16_t fine_y = (ppu.v >> 12) & 0x07;
    uint16_t addr = base + ((uint16_t)ppu.bg_next_tile_id << 4) + fine_y + 8;
    ppu.bg_next_tile_msb = (ppu.cart && ppu.cart->ppu_read)
                               ? ppu.cart->ppu_read(ppu.cart, addr)
                               : 0;
}

// Load the just-fetched tile data into the top of the shift registers.
static void load_background_shifters(void) {
    ppu.bg_shift_pattern_lo = (ppu.bg_shift_pattern_lo & 0xFF00) | ppu.bg_next_tile_lsb;
    ppu.bg_shift_pattern_hi = (ppu.bg_shift_pattern_hi & 0xFF00) | ppu.bg_next_tile_msb;
    // Attribute bits are expanded to a full byte so they can be shifted like
    // the pattern registers (one bit per pixel for the whole 8-pixel span).
    ppu.bg_attr_latch_lo = (ppu.bg_next_tile_attr & 0x01) ? 0xFF : 0x00;
    ppu.bg_attr_latch_hi = (ppu.bg_next_tile_attr & 0x02) ? 0xFF : 0x00;
}

// Shift all background registers left by one. Called every dot in the active
// rendering window so the next pixel is always at the MSB.
static void update_shifters(void) {
    if (ppu.ppumask.bg_render_enable) {
        ppu.bg_shift_pattern_lo <<= 1;
        ppu.bg_shift_pattern_hi <<= 1;
        ppu.bg_shift_attr_lo = (ppu.bg_shift_attr_lo << 1) | ppu.bg_attr_latch_lo;
        ppu.bg_shift_attr_hi = (ppu.bg_shift_attr_hi << 1) | ppu.bg_attr_latch_hi;
    }
}

// ---------------------------------------------------------------------------
// Scroll register updates
// ---------------------------------------------------------------------------

static void increment_coarse_x(void) {
    if (!rendering_enabled()) return;
    if ((ppu.v & 0x001F) == 31) {
        ppu.v &= ~0x001F;
        ppu.v ^= 0x0400; // flip horizontal nametable bit
    } else {
        ppu.v++;
    }
}

static void increment_fine_y(void) {
    if (!rendering_enabled()) return;
    if ((ppu.v & 0x7000) != 0x7000) {
        ppu.v += 0x1000; // increment fine Y
    } else {
        ppu.v &= ~0x7000; // fine Y = 0
        uint16_t coarse_y = (ppu.v & 0x03E0) >> 5;
        if (coarse_y == 29) {
            coarse_y = 0;
            ppu.v ^= 0x0800; // flip vertical nametable bit
        } else if (coarse_y == 31) {
            coarse_y = 0; // wrap in attribute area without flipping
        } else {
            coarse_y++;
        }
        ppu.v = (ppu.v & ~0x03E0) | (coarse_y << 5);
    }
}

// Copy horizontal scroll bits from t into v (dot 257 of each rendered line).
static void copy_x_from_t(void) {
    if (!rendering_enabled()) return;
    // v: ....A.. ...BCDEF = t: ....A.. ...BCDEF
    ppu.v = (ppu.v & 0x7BE0) | (ppu.t & 0x041F);
}

// Copy vertical scroll bits from t into v (dots 280-304 of pre-render line).
static void copy_y_from_t(void) {
    if (!rendering_enabled()) return;
    // v: .IHGFED CBA..... = t: .IHGFED CBA.....
    ppu.v = (ppu.v & 0x041F) | (ppu.t & 0x7BE0);
}

// ---------------------------------------------------------------------------
// Background pixel output
// ---------------------------------------------------------------------------

// Returns the raw 2-bit pixel value (0 = transparent) and sets *palette_idx
// to the 2-bit palette select. Both outputs are needed for sprite compositing.
static uint8_t get_bg_pixel(uint8_t *palette_idx) {
    *palette_idx = 0;
    if (!ppu.ppumask.bg_render_enable) return 0;

    uint8_t bit = 15 - ppu.x;
    uint8_t pixel = (((ppu.bg_shift_pattern_hi >> bit) & 1) << 1) |
                     ((ppu.bg_shift_pattern_lo >> bit) & 1);

    uint8_t attr_bit = 7 - ppu.x;
    *palette_idx = (((ppu.bg_shift_attr_hi >> attr_bit) & 1) << 1) |
                    ((ppu.bg_shift_attr_lo >> attr_bit) & 1);

    return pixel;
}

// ---------------------------------------------------------------------------
// Sprite evaluation and rendering
// ---------------------------------------------------------------------------

static void evaluate_sprites_for_scanline(int16_t next_scanline) {
    ppu.sprite_count = 0;
    uint8_t height = ppu.ppuctrl.sprite_size ? 16 : 8;

    for (int i = 0; i < 64 && ppu.sprite_count < 8; i++) {
        uint8_t sy   = ppu.oam[i * 4 + 0];
        int16_t top  = (int16_t)sy + 1;
        int16_t bot  = top + height;

        if (next_scanline >= top && next_scanline < bot) {
            ppu.secondary_oam[ppu.sprite_count].y             = sy;
            ppu.secondary_oam[ppu.sprite_count].tile          = ppu.oam[i * 4 + 1];
            ppu.secondary_oam[ppu.sprite_count].attr          = ppu.oam[i * 4 + 2];
            ppu.secondary_oam[ppu.sprite_count].x             = ppu.oam[i * 4 + 3];
            ppu.secondary_oam[ppu.sprite_count].is_sprite_zero = (i == 0) ? 1 : 0;
            ppu.sprite_count++;
        }
    }

    if (ppu.sprite_count == 8) {
        for (int i = 8; i < 64; i++) {
            uint8_t sy   = ppu.oam[i * 4 + 0];
            int16_t top  = (int16_t)sy + 1;
            int16_t bot  = top + height;
            if (next_scanline >= top && next_scanline < bot) {
                ppu.ppustatus.sprite_overflow = 1;
                break;
            }
        }
    }
}

// Returns the sprite pixel color (0 = transparent) and sets out_* fields for
// compositing. Scans secondary OAM left to right (lower index = higher priority).
static uint8_t get_sprite_pixel(uint8_t x, uint8_t scanline,
                                uint8_t *out_palette, uint8_t *out_priority,
                                uint8_t *out_is_sprite_zero) {
    *out_palette = 0;
    *out_priority = 0;
    *out_is_sprite_zero = 0;

    if (!ppu.ppumask.sprite_render_enable) return 0;

    uint8_t height = ppu.ppuctrl.sprite_size ? 16 : 8;

    for (int i = 0; i < ppu.sprite_count; i++) {
        uint8_t sx = ppu.secondary_oam[i].x;
        if (x < sx || x >= (uint16_t)sx + 8) continue;

        uint8_t attr   = ppu.secondary_oam[i].attr;
        uint8_t pixel_x = x - sx;
        uint8_t pixel_y = (uint8_t)(scanline - (ppu.secondary_oam[i].y + 1));

        if (pixel_y >= height) continue;

        if (attr & 0x40) pixel_x = 7 - pixel_x;      // horizontal flip
        if (attr & 0x80) pixel_y = height - 1 - pixel_y; // vertical flip

        uint16_t tile_addr;
        if (!ppu.ppuctrl.sprite_size) {
            // 8x8: PPUCTRL bit 3 selects the pattern table
            uint16_t base = ppu.ppuctrl.sprite_pattern_table ? 0x1000 : 0x0000;
            tile_addr = base + ((uint16_t)ppu.secondary_oam[i].tile << 4) + pixel_y;
        } else {
            // 8x16: tile bit 0 selects the pattern table; top/bottom half
            uint16_t base = (ppu.secondary_oam[i].tile & 0x01) ? 0x1000 : 0x0000;
            uint8_t tile  = ppu.secondary_oam[i].tile & 0xFE;
            if (pixel_y >= 8) { tile++; pixel_y -= 8; }
            tile_addr = base + ((uint16_t)tile << 4) + pixel_y;
        }

        uint8_t lo = 0, hi = 0;
        if (ppu.cart && ppu.cart->ppu_read) {
            lo = ppu.cart->ppu_read(ppu.cart, tile_addr);
            hi = ppu.cart->ppu_read(ppu.cart, tile_addr + 8);
        }

        uint8_t color = (((hi >> (7 - pixel_x)) & 1) << 1) |
                         ((lo >> (7 - pixel_x)) & 1);

        if (color == 0) continue; // transparent

        *out_palette       = (attr & 0x03) + 4; // sprite palettes 4-7
        *out_priority      = (attr & 0x20) ? 1 : 0;
        *out_is_sprite_zero = ppu.secondary_oam[i].is_sprite_zero;
        return color;
    }

    return 0; // no opaque sprite pixel
}

// ---------------------------------------------------------------------------
// Pixel compositing
// ---------------------------------------------------------------------------

static uint32_t composite_pixel(uint8_t bg_pixel, uint8_t bg_palette,
                                uint8_t sp_pixel, uint8_t sp_palette,
                                uint8_t sp_priority, uint8_t sp_is_zero) {
    // Sprite 0 hit: both pixels opaque, not in the left clip region.
    if (sp_is_zero && bg_pixel != 0 && sp_pixel != 0 &&
        ppu.ppumask.bg_render_enable && ppu.ppumask.sprite_render_enable) {
        ppu.ppustatus.sprite_0_hit = 1;
    }

    uint8_t color_idx;

    if (bg_pixel == 0 && sp_pixel == 0) {
        // Both transparent: backdrop
        color_idx = ppu.palette_table[0];
    } else if (bg_pixel == 0) {
        // Background transparent, sprite visible
        color_idx = ppu.palette_table[sp_palette * 4 + sp_pixel];
    } else if (sp_pixel == 0) {
        // Sprite transparent, background visible
        color_idx = ppu.palette_table[bg_palette * 4 + bg_pixel];
    } else {
        // Both opaque: priority decides
        if (sp_priority) {
            // Sprite behind background
            color_idx = ppu.palette_table[bg_palette * 4 + bg_pixel];
        } else {
            color_idx = ppu.palette_table[sp_palette * 4 + sp_pixel];
        }
    }

    return NES_PALETTE[color_idx & 0x3F];
}

// ---------------------------------------------------------------------------
// Main PPU clock — one dot per call
// ---------------------------------------------------------------------------

static void clock(void) {
    // ----- Visible + pre-render scanlines (-1 to 239) ----------------------
    if (ppu.scanline >= -1 && ppu.scanline <= 239) {

        // Pre-render scanline: clear status flags at dot 1.
        if (ppu.scanline == -1 && ppu.dot == 1) {
            ppu.ppustatus.vblank_started = 0;
            ppu.ppustatus.sprite_0_hit   = 0;
            ppu.ppustatus.sprite_overflow = 0;
            ppu.frame_complete = 0;
        }

        // --- Background tile fetch pipeline ---------------------------------
        // Active in two windows:
        //   dots 2-257: fetch tiles for this scanline's pixels
        //   dots 321-336: prefetch first two tiles of the NEXT scanline
        if ((ppu.dot >= 2 && ppu.dot <= 257) ||
            (ppu.dot >= 321 && ppu.dot <= 336)) {

            update_shifters();

            // Within each 8-dot group the fetches are staggered.
            // Using (dot-1)%8 so that group boundaries align cleanly:
            //   case 0: dot  1, 9, 17 ... 249, 321, 329 — load & NT fetch
            //   case 2: dot  3, 11 ... 251, 323, 331    — AT fetch
            //   case 4: dot  5, 13 ... 253, 325, 333    — PT lo fetch
            //   case 6: dot  7, 15 ... 255, 327, 335    — PT hi fetch
            //   case 7: dot  8, 16 ... 256, 328, 336    — coarse X increment
            // Dot 1 lands on case 0 but is excluded (first pixel output uses
            // data already in the high byte of the shift registers from the
            // previous scanline's prefetch). Dot 321 IS included so the NT
            // fetch for tile 0 of the next scanline reads nametable[v] while
            // v still points to column 0 (before any coarse X increment).
            switch ((ppu.dot - 1) & 7) {
            case 0:
                load_background_shifters();
                fetch_nametable_byte();
                break;
            case 2:
                fetch_attribute_byte();
                break;
            case 4:
                fetch_pattern_low_byte();
                break;
            case 6:
                fetch_pattern_high_byte();
                break;
            case 7:
                increment_coarse_x();
                break;
            }
        }

        // Dot 256: end of visible pixels — increment fine / coarse Y.
        if (ppu.dot == 256) {
            increment_fine_y();
        }

        // Dot 257: copy horizontal scroll position from t into v, and load
        // the shift registers with the tile data that was just fetched.
        if (ppu.dot == 257) {
            load_background_shifters();
            copy_x_from_t();
        }

        // Dots 280-304 on the pre-render scanline: copy vertical scroll bits.
        if (ppu.scanline == -1 && ppu.dot >= 280 && ppu.dot <= 304) {
            copy_y_from_t();
        }

        // Dot 257: evaluate sprites for the NEXT scanline.
        if (ppu.dot == 257 && ppu.scanline >= 0) {
            evaluate_sprites_for_scanline(ppu.scanline + 1);
        }

        // --- Pixel output (visible scanlines only, dots 1-256) --------------
        if (ppu.scanline >= 0 && ppu.dot >= 1 && ppu.dot <= 256) {
            uint8_t bg_pixel, bg_palette;
            bg_pixel = get_bg_pixel(&bg_palette);

            uint8_t sp_pixel, sp_palette, sp_priority, sp_is_zero;
            sp_pixel = get_sprite_pixel(ppu.dot - 1, ppu.scanline,
                                        &sp_palette, &sp_priority, &sp_is_zero);

            if (ppu.frame_buffer) {
                int idx = ppu.scanline * 256 + (ppu.dot - 1);
                ppu.frame_buffer[idx] = composite_pixel(bg_pixel, bg_palette,
                                                        sp_pixel, sp_palette,
                                                        sp_priority, sp_is_zero);
            }
        }
    }

    // ----- VBlank -----------------------------------------------------------
    if (ppu.scanline == 241 && ppu.dot == 1) {
        ppu.ppustatus.vblank_started = 1;
        ppu.frame_complete = 1;
        if (ppu.ppuctrl.nmi) {
            ppu.nmi_triggered = 1;
        }
    }

    // ----- Advance counters -------------------------------------------------
    ppu.dot++;
    if (ppu.dot > 340) {
        ppu.dot = 0;
        ppu.scanline++;
        if (ppu.scanline > 260) {
            ppu.scanline = -1;
        }
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void connect_bus(void *bus) { ppu.bus = (struct nesbus *)bus; }

static void set_framebuffer(uint32_t *fb) {
    ppu.frame_buffer = fb;
    ppu.scanline = -1;
    ppu.dot = 0;
    ppu.frame_complete = 0;
    ppu.palette_table[0] = 0x0F; // black on power-on
}

static void reset(void) {
    ppu.v = 0;
    ppu.t = 0;
    ppu.x = 0;
    ppu.w = 0;
    ppu.ppuaddr_latch = 0;
    ppu.bg_shift_pattern_lo = 0;
    ppu.bg_shift_pattern_hi = 0;
    ppu.bg_shift_attr_lo    = 0;
    ppu.bg_shift_attr_hi    = 0;
    ppu.bg_attr_latch_lo    = 0;
    ppu.bg_attr_latch_hi    = 0;
    ppu.bg_next_tile_id     = 0;
    ppu.bg_next_tile_attr   = 0;
    ppu.bg_next_tile_lsb    = 0;
    ppu.bg_next_tile_msb    = 0;
}

struct ppu2c02 *ppu2c02_init(void) {
    ppu.cpu_read          = cpu_read;
    ppu.cpu_write         = cpu_write;
    ppu.ppu_read          = ppu_read;
    ppu.ppu_write         = ppu_write;
    ppu.clock             = clock;
    ppu.connect_bus       = connect_bus;
    ppu.reset             = reset;
    ppu.connect_cartridge = connect_cartridge;
    ppu.set_framebuffer   = set_framebuffer;
    return &ppu;
}
