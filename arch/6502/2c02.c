#include "2c02.h"

#include "debug.h"
#include "palette.h"

// PPU Implementation: Backgrounds and Sprites
// - Static background rendering (no scrolling)
// - Sprite evaluation and rendering
// - Sprite 0 hit detection
// - 4 background palettes + 4 sprite palettes

// Debug logging control
static int debug_frame_count = 0;
static struct ppu2c02 ppu = {0};

static void connect_cartridge(struct nes_cartridge *cartridge) {
    ppu.cart = cartridge;
}

static uint16_t nametable_mirror(uint16_t addr) {
    uint16_t mirror_addr;

    //      (0,0)     (256,0)     (511,0)
    //        +-----------+-----------+
    //        |           |           |
    //        |           |           |
    //        |   $2000   |   $2400   |
    //        |           |           |
    //        |           |           |
    // (0,240)+-----------+-----------+(511,240)
    //        |           |           |
    //        |           |           |
    //        |   $2800   |   $2C00   |
    //        |           |           |
    //        |           |           |
    //        +-----------+-----------+
    //      (0,479)   (256,479)   (511,479)

    // Guard against NULL cartridge pointer (can happen during initialization)
    if (!ppu.cart || !ppu.cart->hdr) {
        // Default to horizontal mirroring if cartridge not yet loaded
        // This mirrors the 2KB of nametable RAM
        mirror_addr = (addr - 0x2000) % 0x800;
        return mirror_addr;
    }

    // Normalize address to $2000-$2FFF range (handle mirrors)
    addr = 0x2000 + ((addr - 0x2000) % 0x1000);

    // Get nametable index (0-3)
    uint8_t nametable = (addr >> 10) & 0x03; // Bits 10-11

    if (ppu.cart->hdr->flags6.mirroring == 0) {
        // Horizontal mirroring (vertical arrangement)
        // $2000 = $2400, $2800 = $2C00
        // Map: 0->0, 1->0, 2->1, 3->1
        if (nametable == 0 || nametable == 1) {
            mirror_addr = (addr & 0x03FF); // First 1KB
        } else {
            mirror_addr = 0x0400 | (addr & 0x03FF); // Second 1KB
        }
    } else {
        // Vertical mirroring (horizontal arrangement)
        // $2000 = $2800, $2400 = $2C00
        // Map: 0->0, 1->1, 2->0, 3->1
        if (nametable == 0 || nametable == 2) {
            mirror_addr = (addr & 0x03FF); // First 1KB
        } else {
            mirror_addr = 0x0400 | (addr & 0x03FF); // Second 1KB
        }
    }

    return mirror_addr;
}

static uint8_t ppu_read(uint16_t addr) {
    uint8_t data = 0;

    if (addr < 0x2000) {
        // Pattern table (CHR ROM) - accessed through cartridge
        if (ppu.cart && ppu.cart->ppu_read) {
            data = ppu.cart->ppu_read(ppu.cart, addr);
        } else {
            // Cartridge not loaded yet, return 0
            data = 0;
        }
    } else if (addr >= 0x2000 && addr <= 0x3eff) {
        printf("nametable read %04x\n", addr);
        data = ppu.nametable[nametable_mirror(addr)];

    } else if (addr >= 0x3f00 && addr <= 0x3fff) {
        // palette
        printf("Palette READ\n");
        data = ppu.palette_table[addr & 0x1f];
    } else if (addr >= 0x4000) {
        // [0x4000, 0xFFFF]
        // 	These addresses are mirrors of the the of the
        // memory space [0, 0x3FFF], that is, any address
        // that falls in here, it is accessed by data[addr & 0x3FFF].
        data = ppu_read(addr & 0x3fff);
    }

    return data;
}

static void ppu_write(uint16_t addr, uint8_t data) {
    if (addr < 0x2000) {
        // Pattern table (CHR ROM/RAM) - accessed through cartridge
        if (ppu.cart && ppu.cart->ppu_write) {
            ppu.cart->ppu_write(ppu.cart, addr, data);
        }
        // If cartridge not loaded, silently ignore write
    } else if (addr >= 0x2000 && addr <= 0x3eff) {
        // printf("nametable write %04x : %02x\n", nametable_mirror(addr),
        // data);
        ppu.nametable[nametable_mirror(addr)] = data;
        dump_nametable(ppu.nametable);
    } else if (addr >= 0x3f00 && addr <= 0x3fff) {
        // palette
        printf("Palette WRITE %04x %02x\n", addr, data);
        ppu.palette_table[addr & 0x1f] = data;
    } else if (addr >= 0x4000) {
        // [0x4000, 0xFFFF]
        // 	These addresses are mirrors of the the of the
        // memory space [0, 0x3FFF], that is, any address
        // that falls in here, it is accessed by data[addr & 0x3FFF].
        return ppu_write(addr & 0x3fff, data);
    }
}

// Commmunication with the CPU is done via the special registers at
// $2000-$2007 (mirrored for 0x1000)

static uint8_t cpu_read(uint16_t addr) {
    uint8_t data = 0; // Initialize to avoid undefined behavior
    // printf("CPU read from %04x\n", addr);

    switch (addr & 0x2007) {
    case PPUCTRL:
        // WRITE ONLY
        printf("PPUCTRL is write only\n");
        // exit(1);
        break;

    case PPUMASK:
        // WRITE ONLY
        printf("PPUMASK is write only\n");
        // exit(1);
        break;

    case PPUSTATUS:
        // The act of reading this register resets vblank and w latch
        data = ppu.ppustatus.reg;
        ppu.ppustatus.vblank_started = 0;
        ppu.w = 0; // Reset write latch
        break;

    case OAMADDR:
        // Reading OAMADDR returns open bus (undefined behavior)
        data = 0;
        break;

    case OAMDATA:
        // Return data at current OAM address
        data = ppu.oam[ppu.oamaddr];
        break;

    case PPUSCROLL:
        //
        break;

    case PPUADDR:
        // Reading returns open bus...
        break;

    case PPUDATA:
        // PPUDATA read buffering behavior:
        // - Reads from $0000-$3EFF are buffered (delayed by one read)
        // - Reads from $3F00-$3FFF (palette) are immediate, but still fill
        // buffer

        uint16_t addr = ppu.v & 0x3FFF; // Use v register as address

        if (addr >= 0x3F00 && addr <= 0x3FFF) {
            // Palette reads bypass buffer (immediate)
            data = ppu_read(addr);
            // But buffer is still filled with nametable data "underneath"
            ppu.ppudata_read_buffer = ppu_read(addr & 0x2FFF);
        } else {
            // Buffered read: return previous buffer contents
            data = ppu.ppudata_read_buffer;
            // Fill buffer with new data for next read
            ppu.ppudata_read_buffer = ppu_read(addr);
        }

        // Auto-increment v register based on PPUCTRL bit 2
        ppu.v += (ppu.ppuctrl.vram_addr_increment) ? 32 : 1;
        break;
    }

    return data;
}

static void cpu_write(uint16_t addr, uint8_t data) {
    // printf("CPU write %04x DATA %02x\n", addr, data);
    switch (addr & 0x2007) {
    case PPUCTRL:
        ppu.ppuctrl.reg = data;
        // PPUCTRL: Nametable select bits also affect t register
        //   t: ....BA.. ........ = d: ......BA
        ppu.t = (ppu.t & 0xF3FF) | ((data & 0x03) << 10);
        break;

    case PPUMASK:
        ppu.ppumask.reg = data;
        /*
        printf(
            "  -> PPUMASK write: reg=0x%02x gray=%d bg_left8=%d spr_left8=%d "
            "bg_enable=%d spr_enable=%d emph_r=%d emph_g=%d emph_b=%d\n",
            data, ppu.ppumask.grayscale, ppu.ppumask.bg_enable,
            ppu.ppumask.sprite_enable, ppu.ppumask.bg_render_enable,
            ppu.ppumask.sprite_render_enable, ppu.ppumask.intensify_red,
            ppu.ppumask.intensify_green, ppu.ppumask.intensify_blue);
        */
        break;

    case PPUSTATUS:
        // READ ONLY
        printf("PPUSTATUS is read only\n");
        // exit(1);
        break;

    case OAMADDR:
        // Set OAM address register
        ppu.oamaddr = data;
        break;

    case OAMDATA:
        // Write data to OAM at current address, then increment
        ppu.oam[ppu.oamaddr] = data;
        ppu.oamaddr++; // Auto-increment (wraps at 256)
        break;

    case PPUSCROLL:
        // PPUSCROLL: Two writes via shared w latch
        // First write (w=0): Horizontal scroll
        //   t: ........ ...HGFED = d: HGFED...
        //   x:               CBA = d: .....CBA
        //   w:                   = 1
        // Second write (w=1): Vertical scroll
        //   t: .CBA..HG FED..... = d: HGFEDCBA
        //   w:                   = 0
        if (ppu.w == 0) {
            // First write: horizontal scroll
            uint16_t old_t = ppu.t;
            uint8_t old_x = ppu.x;
            ppu.t = (ppu.t & 0xFFE0) | (data >> 3); // Coarse X
            ppu.x = data & 0x07;                    // Fine X
            ppu.w = 1;
            if (debug_frame_count < 3) {
                printf("[PPUSCROLL] Frame %d: X write data=%02x, t: "
                       "%04x->%04x, x: %d->%d\n",
                       debug_frame_count, data, old_t, ppu.t, old_x, ppu.x);
            }
        } else {
            // Second write: vertical scroll
            uint16_t old_t = ppu.t;
            ppu.t = (ppu.t & 0x8FFF) | ((data & 0x07) << 12); // Fine Y
            ppu.t = (ppu.t & 0xFC1F) | ((data & 0xF8) << 2);  // Coarse Y
            ppu.w = 0;
            if (debug_frame_count < 3) {
                printf(
                    "[PPUSCROLL] Frame %d: Y write data=%02x, t: %04x->%04x\n",
                    debug_frame_count, data, old_t, ppu.t);
            }
        }
        break;

    case PPUADDR:
        // PPUADDR: Two writes via shared w latch
        // First write (w=0): High byte
        //   t: ..FEDCBA ........ = d: ..FEDCBA
        //   t: .X...... ........ = 0
        //   w:                   = 1
        // Second write (w=1): Low byte
        //   t: ........ HGFEDCBA = d: HGFEDCBA
        //   v                    = t
        //   w:                   = 0
        if (ppu.w == 0) {
            // First write: high byte
            ppu.t = (ppu.t & 0x00FF) | ((data & 0x3F) << 8);
            if (debug_frame_count < 3) {
                printf("[PPUADDR] Frame %d: Hi write data=%02x, t=%04x, w→1\n",
                       debug_frame_count, data, ppu.t);
            }
            ppu.w = 1;
        } else {
            // Second write: low byte
            ppu.t = (ppu.t & 0xFF00) | data;
            ppu.v = ppu.t; // Copy t to v
            ppu.w = 0;
            if (debug_frame_count < 3) {
                printf("[PPUADDR] Frame %d: Lo write data=%02x, t=%04x, v←t, "
                       "w→0\n",
                       debug_frame_count, data, ppu.t);
            }
        }
        // Keep legacy ppuaddr for compatibility (until we remove it)
        if (ppu.ppuaddr_latch == 0) {
            ppu.ppuaddr = (data << 8);
            ppu.ppuaddr_latch = 1;
        } else {
            ppu.ppuaddr |= data;
            ppu.ppuaddr_latch = 0;
        }
        break;

    case PPUDATA:
        ppu_write(ppu.ppuaddr, data);
        // auto increment based on ctrl register
        ppu.ppuaddr += (ppu.ppuctrl.vram_addr_increment) ? 32 : 1;
        break;
    }
}

// Helper: Get color from palette index
static uint32_t get_palette_color(uint8_t palette_index) {
    return NES_PALETTE[palette_index & 0x3F];
}

// Fetch background tile data during the 8-dot tile cycle
// These functions are called at specific dots to fetch tile data in advance
static void fetch_nametable_byte(void) {
    // Fetch tile index from nametable using current v register
    uint16_t addr = 0x2000 | (ppu.v & 0x0FFF);
    ppu.bg_next_tile_id = ppu.nametable[nametable_mirror(addr)];
}

static void fetch_attribute_byte(void) {
    // Fetch attribute byte using coarse X/Y from v register
    uint16_t addr = 0x23C0 | (ppu.v & 0x0C00) | ((ppu.v >> 4) & 0x38) |
                    ((ppu.v >> 2) & 0x07);
    uint8_t attr_byte = ppu.nametable[nametable_mirror(addr)];

    // Select 2-bit palette based on position within 4x4 tile group
    uint8_t shift = ((ppu.v >> 4) & 0x04) | (ppu.v & 0x02);
    ppu.bg_next_tile_attr = (attr_byte >> shift) & 0x03;
}

static void fetch_pattern_low_byte(void) {
    // Fetch low bit plane from pattern table
    uint16_t pattern_base = ppu.ppuctrl.bg_pattern_table ? 0x1000 : 0x0000;
    uint16_t fine_y = (ppu.v >> 12) & 0x07;
    uint16_t addr = pattern_base + (ppu.bg_next_tile_id * 16) + fine_y;

    if (ppu.cart && ppu.cart->ppu_read) {
        ppu.bg_next_tile_lsb = ppu.cart->ppu_read(ppu.cart, addr);
    } else {
        ppu.bg_next_tile_lsb = 0;
    }
}

static void fetch_pattern_high_byte(void) {
    // Fetch high bit plane from pattern table
    uint16_t pattern_base = ppu.ppuctrl.bg_pattern_table ? 0x1000 : 0x0000;
    uint16_t fine_y = (ppu.v >> 12) & 0x07;
    uint16_t addr = pattern_base + (ppu.bg_next_tile_id * 16) + fine_y + 8;

    if (ppu.cart && ppu.cart->ppu_read) {
        ppu.bg_next_tile_msb = ppu.cart->ppu_read(ppu.cart, addr);
    } else {
        ppu.bg_next_tile_msb = 0;
    }
}

// Background pixel rendering for sprite compositing
// Returns palette index for the background pixel at (x, y)
// Uses static nametable $2000 (no scrolling)
static uint8_t render_background_pixel_level1(uint8_t x, uint8_t y) {
    // Check if background rendering is enabled
    if (!ppu.ppumask.bg_render_enable) {
        // Return backdrop color palette index
        return ppu.palette_table[0];
    }

    // Direct tile calculation (no scrolling - always shows nametable $2000
    // top-left)
    uint16_t tile_x = x / 8; // 0-31
    uint16_t tile_y = y / 8; // 0-29

    // Nametable address (always $2000 for Level 1)
    uint16_t nametable_addr = 0x2000 + (tile_y * 32) + tile_x;
    uint8_t tile_id = ppu.nametable[nametable_mirror(nametable_addr)];

    // Pixel within tile (0-7)
    uint8_t pixel_x = x % 8;
    uint8_t pixel_y = y % 8;

    // Fetch pattern data from CHR-ROM
    uint16_t pattern_base = ppu.ppuctrl.bg_pattern_table ? 0x1000 : 0x0000;
    uint16_t pattern_addr = pattern_base + (tile_id * 16) + pixel_y;

    uint8_t plane0 = 0;
    uint8_t plane1 = 0;
    if (ppu.cart && ppu.cart->ppu_read) {
        plane0 = ppu.cart->ppu_read(ppu.cart, pattern_addr);
        plane1 = ppu.cart->ppu_read(ppu.cart, pattern_addr + 8);
    }

    // Extract 2-bit pixel color
    uint8_t bit0 = (plane0 >> (7 - pixel_x)) & 0x01;
    uint8_t bit1 = (plane1 >> (7 - pixel_x)) & 0x01;
    uint8_t pixel_color = (bit1 << 1) | bit0;

    // Fetch attribute byte for palette selection
    uint16_t attr_x = tile_x / 4; // 0-7
    uint16_t attr_y = tile_y / 4; // 0-7
    uint16_t attr_addr = 0x23C0 + (attr_y * 8) + attr_x;
    uint8_t attr_byte = ppu.nametable[nametable_mirror(attr_addr)];

    // Extract palette index from attribute byte
    uint8_t attr_shift = ((tile_y & 0x02) << 1) | (tile_x & 0x02);
    uint8_t palette_index = (attr_byte >> attr_shift) & 0x03;

    // Return palette index for this pixel
    uint8_t palette_addr;
    if (pixel_color == 0) {
        palette_addr = 0; // Backdrop color (universal background)
    } else {
        // Background palettes start at 0, each palette is 4 colors
        palette_addr = (palette_index * 4) + pixel_color;
    }

    return ppu.palette_table[palette_addr];
}

// Render a single background pixel using shift registers (hardware-accurate)
static uint8_t render_background_pixel(uint8_t x, uint8_t y) {
    (void)x; // Screen coordinates not used
    (void)y;

    if (!ppu.ppumask.bg_render_enable) {
        // Debug: Print on first scanline to see why rendering is disabled
        if (ppu.scanline == 0 && ppu.dot == 1) {
            printf("DEBUG: bg_render_enable=0, PPUMASK=%02x, "
                   "backdrop=palette[0]=%02x -> color=%08x\n",
                   ppu.ppumask.reg, ppu.palette_table[0],
                   NES_PALETTE[ppu.palette_table[0] & 0x3F]);
        }
        return ppu.palette_table[0]; // Backdrop color
    }

    // TEMPORARY: Disable fine X scrolling to diagnose issues
    // Force fine_x to 0 for 8-pixel aligned scrolling only
    uint8_t fine_x = 0; // TODO: Use ppu.x when fine X is debugged

    // Select pixel from shift registers using fine X
    // Shift registers hold 16 bits, we select bit 15-fine_x
    uint8_t bit_select = 15 - fine_x;
    uint8_t pixel_lo = (ppu.bg_shift_pattern_lo >> bit_select) & 0x01;
    uint8_t pixel_hi = (ppu.bg_shift_pattern_hi >> bit_select) & 0x01;
    uint8_t pixel_color = pixel_lo | (pixel_hi << 1);

    // Select palette from attribute shift registers
    // ISSUE: Attribute registers are 8-bit, not 16-bit like pattern registers
    // They should also use fine_x offset, but from bit 7-fine_x
    uint8_t attr_bit_select = 7 - fine_x;
    uint8_t attr_lo = (ppu.bg_shift_attr_lo >> attr_bit_select) & 0x01;
    uint8_t attr_hi = (ppu.bg_shift_attr_hi >> attr_bit_select) & 0x01;
    uint8_t palette_index = attr_lo | (attr_hi << 1);

    // Debug: Sample a pixel to see actual data
    if (ppu.scanline == 100 && ppu.dot == 128 && pixel_color == 0) {
        printf("DEBUG: pixel_color=%d palette[0]=%02x shift_lo=%04x "
               "shift_hi=%04x\n",
               pixel_color, ppu.palette_table[0], ppu.bg_shift_pattern_lo,
               ppu.bg_shift_pattern_hi);
    }

    if (pixel_color == 0) {
        // Transparent - use backdrop color
        return ppu.palette_table[0];
    }

    // Get final color from palette
    uint8_t palette_addr = (palette_index * 4) + pixel_color;
    return ppu.palette_table[palette_addr];
}

// Evaluate sprites for current scanline
// Finds up to 8 sprites that are visible on this scanline
static void evaluate_sprites_for_scanline(int16_t scanline) {
    ppu.sprite_count = 0;
    uint8_t sprite_height = ppu.ppuctrl.sprite_size ? 16 : 8; // 8x8 or 8x16

    // Scan all 64 sprites in OAM
    for (int i = 0; i < 64 && ppu.sprite_count < 8; i++) {
        uint8_t sprite_y = ppu.oam[i * 4 + 0];
        uint8_t tile_index = ppu.oam[i * 4 + 1];
        uint8_t attributes = ppu.oam[i * 4 + 2];
        uint8_t sprite_x = ppu.oam[i * 4 + 3];

        // Check if sprite is on this scanline
        // Y position is scanline where top of sprite appears (sprite drawn on
        // Y+1 to Y+height)
        int16_t sprite_top = sprite_y + 1;
        int16_t sprite_bottom = sprite_y + sprite_height;

        if (scanline >= sprite_top && scanline < sprite_bottom) {
            // Store in secondary OAM
            ppu.secondary_oam[ppu.sprite_count].y = sprite_y;
            ppu.secondary_oam[ppu.sprite_count].tile = tile_index;
            ppu.secondary_oam[ppu.sprite_count].attr = attributes;
            ppu.secondary_oam[ppu.sprite_count].x = sprite_x;
            ppu.sprite_count++;
        }
    }

    // Set sprite overflow flag if more than 8 sprites on scanline
    if (ppu.sprite_count == 8) {
        // Check if there are more sprites
        for (int i = 8 * 4; i < 256; i += 4) {
            uint8_t sprite_y = ppu.oam[i];
            int16_t sprite_top = sprite_y + 1;
            int16_t sprite_bottom = sprite_y + sprite_height;
            if (scanline >= sprite_top && scanline < sprite_bottom) {
                ppu.ppustatus.sprite_overflow = 1;
                break;
            }
        }
    }
}

// Render sprite pixel at given screen coordinates
static uint8_t render_sprite_pixel(uint8_t x, uint8_t y) {
    if (!ppu.ppumask.sprite_render_enable) {
        return 0xFF; // Sprites disabled
    }

    // Check secondary OAM for sprites at this pixel
    for (int i = 0; i < ppu.sprite_count; i++) {
        uint8_t sprite_x = ppu.secondary_oam[i].x;
        uint8_t sprite_y = ppu.secondary_oam[i].y;
        uint8_t tile_index = ppu.secondary_oam[i].tile;
        uint8_t attributes = ppu.secondary_oam[i].attr;

        // Check if pixel is within sprite bounds
        if (x < sprite_x || x >= sprite_x + 8) {
            continue; // Not in this sprite's X range
        }

        // Calculate pixel position within sprite (0-7 or 0-15)
        uint8_t pixel_x = x - sprite_x;
        uint8_t pixel_y = y - (sprite_y + 1); // +1 because Y is scanline-1

        // Validate Y coordinate is within sprite bounds
        uint8_t sprite_height = ppu.ppuctrl.sprite_size ? 16 : 8;
        if (pixel_y >= sprite_height) {
            continue; // Out of bounds, skip this sprite
        }

        // Apply horizontal flip
        if (attributes & 0x40) {
            pixel_x = 7 - pixel_x;
        }

        // Apply vertical flip
        if (attributes & 0x80) {
            pixel_y = sprite_height - 1 - pixel_y;
        }

        // Get pattern table address (sprites use table from PPUCTRL bit 3)
        uint16_t pattern_table_base =
            ppu.ppuctrl.sprite_pattern_table ? 0x1000 : 0x0000;
        uint16_t tile_addr = pattern_table_base + (tile_index * 16);

        // Read pattern data (2 bitplanes) with NULL safety
        uint8_t plane0 = 0;
        uint8_t plane1 = 0;

        if (ppu.cart && ppu.cart->ppu_read) {
            plane0 = ppu.cart->ppu_read(ppu.cart, tile_addr + pixel_y);
            plane1 = ppu.cart->ppu_read(ppu.cart, tile_addr + pixel_y + 8);
        }

        // Extract pixel color (2 bits)
        uint8_t bit0 = (plane0 >> (7 - pixel_x)) & 0x01;
        uint8_t bit1 = (plane1 >> (7 - pixel_x)) & 0x01;
        uint8_t pixel_color = (bit1 << 1) | bit0;

        // If pixel is transparent (color 0), try next sprite
        if (pixel_color == 0) {
            continue;
        }

        // Get sprite palette (bits 0-1 of attributes select palette 0-3)
        // Sprite palettes start at $3F10
        uint8_t palette_index =
            (attributes & 0x03) + 4; // +4 for sprite palettes
        uint8_t palette_addr = (palette_index * 4) + pixel_color;
        uint8_t color = ppu.palette_table[palette_addr];

        // Encode sprite info in upper bits for priority handling
        // Bit 7: 1 = sprite pixel (vs background)
        // Bit 6: priority bit from attributes (0=front, 1=back)
        // Bit 5: sprite 0 flag
        return color | 0x80 | ((attributes & 0x20) << 1) |
               ((i == 0) ? 0x20 : 0);
    }

    return 0xFF; // No sprite pixel
}

// Combine background and sprite pixels with priority handling
static uint8_t combine_pixels(uint8_t bg, uint8_t sprite) {
    // Sprite encoding: bit 7=sprite present, bit 6=priority, bit 5=sprite 0
    // Lower 6 bits = palette index

    if (sprite == 0xFF) {
        return bg; // No sprite, use background
    }

    // Extract sprite info
    uint8_t is_sprite = (sprite & 0x80) ? 1 : 0;
    uint8_t priority_behind = (sprite & 0x40) ? 1 : 0;
    uint8_t is_sprite_0 = (sprite & 0x20) ? 1 : 0;
    uint8_t sprite_color = sprite & 0x1F; // Lower 5 bits

    if (!is_sprite) {
        return bg; // Not actually a sprite
    }

    // Sprite 0 hit detection
    // Set when sprite 0 opaque pixel overlaps background opaque pixel
    if (is_sprite_0 && bg != 0 && sprite_color != 0) {
        ppu.ppustatus.sprite_0_hit = 1;
    }

    // Handle priority
    if (priority_behind) {
        // Sprite behind background: only show if background is transparent
        if (bg == 0) {
            return sprite_color;
        } else {
            return bg;
        }
    } else {
        // Sprite in front: sprite always wins
        return sprite_color;
    }
}

// Increment horizontal position in v register
// Called every 8 dots during rendering
static void increment_coarse_x(void) {
    if ((ppu.v & 0x001F) == 31) { // if coarse X == 31
        ppu.v &= ~0x001F;         // coarse X = 0
        ppu.v ^= 0x0400;          // switch horizontal nametable
    } else {
        ppu.v += 1; // increment coarse X
    }
}

static void clock() {
    // NES PPU timing:
    // Scanlines -1 to 260 (262 total)
    // -1: Pre-render scanline
    // 0-239: Visible scanlines
    // 240: Post-render (idle)
    // 241-260: VBlank

    // Render visible pixels (and pre-render scanline)
    if (ppu.scanline >= -1 && ppu.scanline < 240) {
        // Sprite evaluation at start of scanline
        if (ppu.dot == 1 && ppu.scanline >= 0) {
            // Only evaluate sprites if either bg or sprite rendering is enabled
            if (ppu.ppumask.bg_render_enable ||
                ppu.ppumask.sprite_render_enable) {
                evaluate_sprites_for_scanline(ppu.scanline);
            }
        }

        // Render pixels: background + sprites
        if (ppu.scanline >= 0 && ppu.dot >= 1 && ppu.dot <= 256) {
            // Get background pixel
            uint8_t bg_pixel =
                render_background_pixel_level1(ppu.dot - 1, ppu.scanline);

            // Get sprite pixel
            uint8_t sprite_pixel =
                render_sprite_pixel(ppu.dot - 1, ppu.scanline);

            // Composite background and sprite
            uint8_t final_pixel = combine_pixels(bg_pixel, sprite_pixel);

            // Write to frame buffer
            if (ppu.frame_buffer) {
                int index = ppu.scanline * 256 + (ppu.dot - 1);
                ppu.frame_buffer[index] = get_palette_color(final_pixel);
            }
        }
    }

    // Scanline 241, dot 1: Enter VBlank
    if (ppu.scanline == 241 && ppu.dot == 1) {
        if (debug_frame_count < 3) {
            printf("\n===== Frame %d complete, entering VBlank =====\n\n",
                   debug_frame_count);
        }
        ppu.ppustatus.vblank_started = 1;
        ppu.frame_complete = 1;

        // Trigger NMI if enabled in PPUCTRL (bit 7)
        if (ppu.ppuctrl.nmi) {
            ppu.nmi_triggered = 1;
            printf("PPU: NMI triggered at scanline 241 (VBlank start)\n");
        }
    }

    // Scanline -1 (pre-render), dot 1: Clear VBlank
    if (ppu.scanline == -1 && ppu.dot == 1) {
        ppu.ppustatus.vblank_started = 0;
        ppu.ppustatus.sprite_0_hit = 0;
        // ppu.ppustatus.sprite_overflow = 0;
        ppu.frame_complete = 0;
        // Note: nmi_triggered is NOT cleared here - CPU clears it when
        // servicing NMI
    }

    // No scroll register operations in this version (static backgrounds only)

    // Advance dot counter
    ppu.dot++;
    if (ppu.dot > 340) {
        ppu.dot = 0;
        ppu.scanline++;
        if (ppu.scanline > 260) {
            ppu.scanline = -1; // Reset to pre-render
            debug_frame_count++;
        }
    }
}

static void connect_bus(void *bus) { ppu.bus = (struct nesbus *)bus; }

static void set_framebuffer(uint32_t *fb) {
    ppu.frame_buffer = fb;
    ppu.scanline = -1; // Start at pre-render scanline per NES hardware spec
    ppu.dot = 0;
    ppu.frame_complete = 0;

    // Initialize backdrop color to black (NES power-on default)
    // 0x0F = black in NES palette
    ppu.palette_table[0] = 0x0F;

    printf("PPU: Frame buffer connected at %p\n", (void *)fb);
    printf("PPU: Backdrop color initialized to palette[0]=%02x (black)\n",
           ppu.palette_table[0]);
}

static void reset(void) {
    ppu.ppuaddr_latch = 0;
    // Reset loopy registers
    ppu.v = 0;
    ppu.t = 0;
    ppu.x = 0;
    ppu.w = 0;

    // Reset shift registers
    ppu.bg_shift_pattern_lo = 0;
    ppu.bg_shift_pattern_hi = 0;
    ppu.bg_shift_attr_lo = 0;
    ppu.bg_shift_attr_hi = 0;

    // Reset attribute latches
    ppu.bg_attr_latch_lo = 0;
    ppu.bg_attr_latch_hi = 0;

    // Reset tile fetch latches
    ppu.bg_next_tile_id = 0;
    ppu.bg_next_tile_attr = 0;
    ppu.bg_next_tile_lsb = 0;
    ppu.bg_next_tile_msb = 0;
}

struct ppu2c02 *ppu2c02_init() {
    ppu.cpu_read = cpu_read;
    ppu.cpu_write = cpu_write;
    ppu.ppu_read = ppu_read;
    ppu.ppu_write = ppu_write;
    ppu.clock = clock;
    ppu.connect_bus = connect_bus;
    ppu.reset = reset;
    ppu.connect_cartridge = connect_cartridge;
    ppu.set_framebuffer = set_framebuffer;

    return &ppu;
}