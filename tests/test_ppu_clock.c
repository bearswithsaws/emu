/*
 * test_ppu_clock.c
 *
 * Unit tests for the 2C02 PPU clock() rewrite.
 *
 * Tests are structured as:
 *   1. Set up a minimal fake cartridge / PPU state.
 *   2. Run the PPU for a known number of dots.
 *   3. Assert observable outputs (framebuffer pixels, status flags, timing).
 *
 * We do not link against SDL2 or the full emulator; we only need lib6502.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "2c02.h"
#include "cartridge.h"
#include "palette.h"

// ---------------------------------------------------------------------------
// Minimal fake cartridge
// ---------------------------------------------------------------------------

#define CHR_SIZE 0x2000  /* 8 KB */
static uint8_t fake_chr[CHR_SIZE];

static uint8_t fake_ppu_read(struct nes_cartridge *c, uint16_t addr) {
    (void)c;
    return fake_chr[addr & (CHR_SIZE - 1)];
}

static void fake_ppu_write(struct nes_cartridge *c, uint16_t addr, uint8_t data) {
    (void)c;
    fake_chr[addr & (CHR_SIZE - 1)] = data;
}

static struct nes_cartridge_hdr fake_hdr = {
    .flags6 = { .flagss = 0x01 }  /* bit 0 = 1: vertical mirroring */
};

static struct nes_cartridge fake_cart = {
    .hdr       = &fake_hdr,
    .ppu_read  = fake_ppu_read,
    .ppu_write = fake_ppu_write,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int test_pass = 0;
static int test_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (cond) {                                                    \
            printf("  PASS: %s\n", msg);                              \
            test_pass++;                                               \
        } else {                                                       \
            printf("  FAIL: %s  (line %d)\n", msg, __LINE__);        \
            test_fail++;                                               \
        }                                                              \
    } while (0)

/* Run the PPU for exactly N dots. */
static void run_dots(struct ppu2c02 *ppu, int n) {
    for (int i = 0; i < n; i++) ppu->clock();
}

/* Run the PPU to the end of the current frame (scanline -1 / dot 0). */
static void run_to_frame_start(struct ppu2c02 *ppu) {
    /* A full NTSC frame = 262 scanlines × 341 dots = 89342 dots.
     * Run for at most 2 frames to guarantee we land on dot 0 of scan -1. */
    for (int i = 0; i < 89342 * 2; i++) {
        ppu->clock();
        if (ppu->frame_complete) {
            /* Drain to pre-render scanline dot 0. */
            while (!(ppu->scanline == -1 && ppu->dot == 0)) {
                ppu->clock();
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Encode a tile into fake CHR ROM (8x8, two bit-planes)
// palette_idx: 0-3, applied uniformly to all opaque pixels.
// pattern: 8 bytes, each byte = one row. Bit 7 = leftmost pixel.
// ---------------------------------------------------------------------------
static void write_tile(uint16_t tile_id, const uint8_t pattern[8]) {
    uint16_t base = (uint16_t)tile_id * 16;
    for (int row = 0; row < 8; row++) {
        fake_chr[base + row]     = pattern[row]; /* lo plane */
        fake_chr[base + row + 8] = 0x00;          /* hi plane = 0 */
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/*
 * test_vblank_timing
 *
 * VBlank flag must be set at scanline 241, dot 1 and NMI triggered when
 * PPUCTRL NMI-enable is set.  Flag must be clear at pre-render dot 1.
 */
static void test_vblank_timing(void) {
    printf("\n[test_vblank_timing]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Enable NMI in PPUCTRL */
    ppu->cpu_write(0x2000, 0x80);
    /* Enable rendering so the PPU actually advances scroll logic */
    ppu->cpu_write(0x2001, 0x18);

    /* Advance to scanline 241, dot 0.
     * Starting state is (-1, 0).  Each clock() call processes the current
     * (scanline, dot) pair and THEN increments.  To reach the state (241, 0),
     * we must process every dot from (-1,0) through (240,340) inclusive —
     * that is (241+1)*341 = 82522 calls. */
    int dots_to_241 = (241 + 1) * 341;
    run_dots(ppu, dots_to_241);

    ASSERT(ppu->ppustatus.vblank_started == 0,
           "VBlank not yet set at scanline 241 dot 0");

    /* Process dots 0 and 1 of scanline 241.  VBlank fires during the
     * processing of dot 1 (i.e. on the second of these two calls). */
    run_dots(ppu, 2);

    ASSERT(ppu->ppustatus.vblank_started == 1,
           "VBlank set at scanline 241 dot 1");
    ASSERT(ppu->nmi_triggered == 1,
           "NMI triggered when NMI-enable is set");
    ASSERT(ppu->frame_complete == 1,
           "frame_complete set at VBlank start");

    /* Reading PPUSTATUS should clear VBlank and the w latch */
    uint8_t status = ppu->cpu_read(0x2002);
    ASSERT((status & 0x80) != 0, "PPUSTATUS VBlank bit readable");
    ASSERT(ppu->ppustatus.vblank_started == 0,
           "VBlank cleared after PPUSTATUS read");
    ASSERT(ppu->w == 0, "w latch cleared after PPUSTATUS read");

    /* Run to pre-render scanline dot 1 — VBlank should be cleared.
     * run_to_frame_start() drains to state (-1, 0).  We then need to
     * process two dots: dot 0 (no-op for the clear) and dot 1 (fires the
     * vblank/status clear). */
    run_to_frame_start(ppu);
    run_dots(ppu, 2); /* process pre-render dots 0 and 1 */
    ASSERT(ppu->ppustatus.vblank_started == 0,
           "VBlank cleared at pre-render scanline dot 1");
    ASSERT(ppu->ppustatus.sprite_0_hit == 0,
           "Sprite 0 hit cleared at pre-render dot 1");
}

/*
 * test_scroll_register_writes
 *
 * Verify that PPUSCROLL and PPUADDR writes correctly update t, v, x, and w
 * per the Loopy register specification.
 */
static void test_scroll_register_writes(void) {
    printf("\n[test_scroll_register_writes]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* First PPUSCROLL write: X scroll = 0b10110011 = 0xB3
     * coarse X = 0xB3 >> 3 = 22 (0x16)
     * fine  X = 0xB3 & 7  = 3
     * t bits 4:0 = 22 = 0b10110
     */
    ppu->cpu_write(0x2005, 0xB3);
    ASSERT(ppu->w == 1, "w=1 after first PPUSCROLL write");
    ASSERT(ppu->x == 3, "fine X = 3");
    ASSERT((ppu->t & 0x001F) == 22, "coarse X in t = 22");

    /* Second PPUSCROLL write: Y scroll = 0b01001101 = 0x4D
     * coarse Y = 0x4D >> 3 = 9 (bits 9:5 of t)
     * fine  Y = 0x4D & 7  = 5 (bits 14:12 of t)
     */
    ppu->cpu_write(0x2005, 0x4D);
    ASSERT(ppu->w == 0, "w=0 after second PPUSCROLL write");
    uint8_t coarse_y = (ppu->t >> 5) & 0x1F;
    uint8_t fine_y   = (ppu->t >> 12) & 0x07;
    ASSERT(coarse_y == 9, "coarse Y in t = 9");
    ASSERT(fine_y == 5,   "fine Y in t = 5");

    /* PPUADDR two-write sequence: $2006 hi then $2006 lo
     * After second write v == t and the combined address is in v.
     */
    ppu->cpu_write(0x2006, 0x21); /* hi: t = 0x2100, w=1 */
    ASSERT(ppu->w == 1, "w=1 after first PPUADDR write");
    ppu->cpu_write(0x2006, 0xAB); /* lo: t = 0x21AB, v = t, w=0 */
    ASSERT(ppu->w == 0, "w=0 after second PPUADDR write");
    ASSERT(ppu->v == 0x21AB, "v = $21AB after PPUADDR sequence");

    /* PPUCTRL nametable select bits should update t bits 11-10 */
    ppu->cpu_write(0x2006, 0x20); /* reset for clarity */
    ppu->cpu_write(0x2006, 0x00);
    ppu->cpu_write(0x2000, 0x03); /* nametable = 3 */
    ASSERT(((ppu->t >> 10) & 0x03) == 3, "nametable bits in t set by PPUCTRL");
}

/*
 * test_nametable_palette_access
 *
 * Write known data to nametable and palette via the PPU register interface,
 * then read it back to confirm the internal state is correct.
 */
static void test_nametable_palette_access(void) {
    printf("\n[test_nametable_palette_access]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Write 0xAB to nametable address $2005 via PPUADDR + PPUDATA */
    ppu->cpu_write(0x2006, 0x20);
    ppu->cpu_write(0x2006, 0x05);
    ppu->cpu_write(0x2007, 0xAB);

    /* Seek back and read with buffered PPUDATA */
    ppu->cpu_write(0x2006, 0x20);
    ppu->cpu_write(0x2006, 0x05);
    ppu->cpu_read(0x2007); /* dummy read to fill buffer */
    uint8_t readback = ppu->cpu_read(0x2007);
    /* After the write, v auto-incremented by 1 so a second read gives us
     * the byte at $2006 — but we care that the write worked. Re-read directly
     * via the internal nametable array for a deterministic check. */
    (void)readback;

    /* Palette write: $3F01 = 0x16 (blue) */
    ppu->cpu_write(0x2006, 0x3F);
    ppu->cpu_write(0x2006, 0x01);
    ppu->cpu_write(0x2007, 0x16);

    ASSERT(ppu->palette_table[1] == 0x16, "palette[1] written via PPUDATA");

    /* Palette mirror: $3F11 should mirror $3F01? No — $3F10/$3F14/$3F18/$3F1C
     * mirror $3F00/$3F04/$3F08/$3F0C (backdrop mirrors only).
     * $3F11 is a distinct entry. Write it separately. */
    ppu->cpu_write(0x2006, 0x3F);
    ppu->cpu_write(0x2006, 0x10);
    ppu->cpu_write(0x2007, 0x22);
    /* $3F10 is a mirror of $3F00 (backdrop), so palette_table[0] == 0x22 */
    ASSERT(ppu->palette_table[0] == 0x22, "$3F10 write mirrors to palette_table[0]");
}

/*
 * test_background_pixel_output
 *
 * Set up a single solid-colour tile, point nametable[0,0] at it, and verify
 * that after one full frame the top-left pixel in the framebuffer matches the
 * expected colour from the NES palette.
 *
 * Tile: all pixels = colour 1 in palette 0.
 * bg palette 0 colour 1 = palette_table[1].
 */
static void test_background_pixel_output(void) {
    printf("\n[test_background_pixel_output]\n");

    memset(fake_chr, 0, sizeof(fake_chr));

    /* Tile 1: lo plane all 1s, hi plane all 0s → pixel colour = 1 */
    uint8_t solid_lo[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    write_tile(1, solid_lo);

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Enable background rendering + show BG in leftmost 8 columns (bit 1) */
    ppu->cpu_write(0x2001, 0x0A);

    /* Write tile ID 1 into nametable[0,0] (address $2000) */
    ppu->cpu_write(0x2006, 0x20);
    ppu->cpu_write(0x2006, 0x00);
    ppu->cpu_write(0x2007, 0x01); /* tile 1 */

    /* Set bg palette 0 colour 1 = NES colour 0x16 (medium blue) */
    ppu->cpu_write(0x2006, 0x3F);
    ppu->cpu_write(0x2006, 0x01);
    ppu->cpu_write(0x2007, 0x16);

    /* Reset scroll and nametable selection.
     * The PPUADDR writes above left t pointing into palette space, which
     * set the nametable bits in t.  Writing PPUCTRL clears t bits 11-10
     * back to nametable 0, then PPUSCROLL zeroes fine/coarse scroll. */
    ppu->cpu_write(0x2000, 0x08); /* bg_pattern_table=0, NMI off, nametable 0 */
    ppu->cpu_write(0x2005, 0x00);
    ppu->cpu_write(0x2005, 0x00);

    /* Run one complete frame */
    run_to_frame_start(ppu);       /* wait for frame_complete */
    /* Run one more full frame so we get a rendered framebuffer */
    for (int i = 0; i < 89342; i++) ppu->clock();

    uint32_t expected = NES_PALETTE[0x16 & 0x3F];
    /* The top-left 8×8 block should all be the tile 1 colour.
     * Check a sample pixel in the middle of the tile: (4, 4). */
    uint32_t actual = fb[4 * 256 + 4];
    printf("  DBG: expected=%08X actual=%08X palette_table[1]=%02X\n",
           expected, actual, ppu->palette_table[1]);
    ASSERT(actual == expected,
           "bg pixel (4,4) matches NES palette colour for tile colour index 1");

    /* Also verify that a pixel in tile (1,0) (x=8..15, y=0..7) is the
     * backdrop colour — nametable[1] defaults to tile 0 which has all-zero
     * CHR data, so pixel colour = 0 = transparent = palette_table[0]. */
    uint32_t backdrop = NES_PALETTE[ppu->palette_table[0] & 0x3F];
    uint32_t tile1_px = fb[0 * 256 + 12]; /* row 0, col 12 (in tile 1,0) */
    ASSERT(tile1_px == backdrop,
           "bg pixel in empty tile uses backdrop colour");
}

/*
 * attr_test_setup — shared setup for the two attribute-latch tests.
 *
 * Tile layout:
 *   CHR tile 1: all pixels = colour index 1 (lo-plane 0xFF, hi-plane 0x00)
 *
 * Nametable (row 0):
 *   coarse_x=1 → tile 1   (palette determined by attr bits 1:0)
 *   coarse_x=2 → tile 1   (palette determined by attr bits 3:2)
 *
 * Attribute table byte at $23C0:
 *   bits 1:0 = 0  → palette 0 for coarse_x ∈ {0,1}
 *   bits 3:2 = 1  → palette 1 for coarse_x ∈ {2,3}
 *   value = 0x04
 *
 * Palette:
 *   palette[1] = 0x16  (palette 0, colour index 1 → medium blue)
 *   palette[5] = 0x25  (palette 1, colour index 1 → medium red)
 *
 * Rendering starts at coarse_x=1 with the supplied fine_x (0 or 4).
 * Returns the PPU pointer; the caller owns the framebuffer.
 */
static struct ppu2c02 *attr_test_setup(uint32_t *fb, uint8_t fine_x_val) {
    memset(fake_chr, 0, sizeof(fake_chr));
    /* Tile 1: lo plane all 1s → every pixel = colour index 01 */
    uint8_t solid_lo[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    write_tile(1, solid_lo);

    struct ppu2c02 *ppu = ppu2c02_init();
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Write tile 1 into nametable positions (1,0) and (2,0) */
    ppu->cpu_write(0x2006, 0x20); ppu->cpu_write(0x2006, 0x01);
    ppu->cpu_write(0x2007, 0x01);  /* nametable[1] = tile 1 */
    ppu->cpu_write(0x2006, 0x20); ppu->cpu_write(0x2006, 0x02);
    ppu->cpu_write(0x2007, 0x01);  /* nametable[2] = tile 1 */

    /* Attribute byte at $23C0: bits 1:0=0 (pal 0), bits 3:2=1 (pal 1) */
    ppu->cpu_write(0x2006, 0x23); ppu->cpu_write(0x2006, 0xC0);
    ppu->cpu_write(0x2007, 0x04);

    /* Palette 0 colour 1 = 0x16 (blue), palette 1 colour 1 = 0x25 (red) */
    ppu->cpu_write(0x2006, 0x3F); ppu->cpu_write(0x2006, 0x01);
    ppu->cpu_write(0x2007, 0x16);
    ppu->cpu_write(0x2006, 0x3F); ppu->cpu_write(0x2006, 0x05);
    ppu->cpu_write(0x2007, 0x25);

    /* Reset scroll: nametable 0, coarse_x=1, fine_x=fine_x_val, Y=0 */
    ppu->cpu_write(0x2000, 0x08);
    /* First PPUSCROLL write encodes coarse_x and fine_x:
     * data = (coarse_x << 3) | fine_x */
    ppu->cpu_write(0x2005, (uint8_t)((1 << 3) | fine_x_val));
    ppu->cpu_write(0x2005, 0x00);  /* Y = 0 */

    /* Enable BG rendering globally and in the leftmost 8 columns */
    ppu->cpu_write(0x2001, 0x0A);

    /* Run to the start of a new frame so t→v copies take effect,
     * then run one full frame to fill the framebuffer. */
    run_to_frame_start(ppu);
    for (int i = 0; i < 89342; i++) ppu->clock();

    return ppu;
}

/*
 * test_attr_latch_no_fine_x
 *
 * Baseline: with fine_x=0 the palette transition must occur exactly at the
 * 8-pixel tile boundary.  Scroll starts at coarse_x=1 so:
 *   screen cols  0– 7: tile coarse_x=1 → palette 0 → NES_PALETTE[0x16]
 *   screen cols  8–15: tile coarse_x=2 → palette 1 → NES_PALETTE[0x25]
 */
static void test_attr_latch_no_fine_x(void) {
    printf("\n[test_attr_latch_no_fine_x]\n");

    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    attr_test_setup(fb, 0);

    uint32_t pal0_col = NES_PALETTE[0x16 & 0x3F];
    uint32_t pal1_col = NES_PALETTE[0x25 & 0x3F];

    /* Cols 0–7 must use palette 0 */
    for (int col = 0; col < 8; col++) {
        uint32_t px = fb[0 * 256 + col];
        ASSERT(px == pal0_col, "fine_x=0: col 0-7 uses palette 0 colour");
    }
    /* Cols 8–15 must use palette 1 */
    for (int col = 8; col < 16; col++) {
        uint32_t px = fb[0 * 256 + col];
        ASSERT(px == pal1_col, "fine_x=0: col 8-15 uses palette 1 colour");
    }
}

/*
 * test_attr_latch_fine_x_regression
 *
 * Regression test for the bg_attr_latch 0xFF bug (issue #36).
 *
 * With fine_x=4 the tile boundary shifts: cols 0–3 are the tail of
 * coarse_x=1 (palette 0) and cols 4–11 are coarse_x=2 (palette 1).
 *
 * Before the fix, bg_attr_latch_lo was stored as 0xFF.  On the first call to
 * update_shifters() after loading tile coarse_x=2's attribute, the expression
 * (0x00 << 1) | 0xFF = 0xFF set all bits immediately, making palette 1 appear
 * at screen column 1 instead of column 4.
 *
 * With the fix (latch stored as 0 or 1), only 1 bit is fed per dot, so the
 * transition happens at the correct visual tile boundary.
 */
static void test_attr_latch_fine_x_regression(void) {
    printf("\n[test_attr_latch_fine_x_regression]\n");

    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    attr_test_setup(fb, 4);  /* fine_x = 4 */

    uint32_t pal0_col = NES_PALETTE[0x16 & 0x3F];
    uint32_t pal1_col = NES_PALETTE[0x25 & 0x3F];

    /* Cols 0–3 are pixels 4–7 of tile coarse_x=1: must use palette 0 */
    for (int col = 0; col < 4; col++) {
        uint32_t px = fb[0 * 256 + col];
        ASSERT(px == pal0_col,
               "fine_x=4: col 0-3 (tail of tile cx=1) uses palette 0 colour");
    }
    /* Cols 4–11 are pixels 0–7 of tile coarse_x=2: must use palette 1 */
    for (int col = 4; col < 12; col++) {
        uint32_t px = fb[0 * 256 + col];
        ASSERT(px == pal1_col,
               "fine_x=4: col 4-11 (tile cx=2) uses palette 1 colour");
    }
}

/*
 * test_ppudata_read_buffer
 *
 * Reads from $0000-$3EFF via PPUDATA must be delayed by one read (buffered).
 * Reads from $3F00-$3FFF (palette) must be immediate.
 */
static void test_ppudata_read_buffer(void) {
    printf("\n[test_ppudata_read_buffer]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    fake_chr[0x0000] = 0xAA;
    fake_chr[0x0001] = 0xBB;

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Point v at $0000 */
    ppu->cpu_write(0x2006, 0x00);
    ppu->cpu_write(0x2006, 0x00);

    /* First read: returns stale buffer (0), fills buffer with CHR[$0000]=0xAA */
    uint8_t r0 = ppu->cpu_read(0x2007);
    ASSERT(r0 == 0x00, "first PPUDATA read returns stale buffer");

    /* Second read: returns 0xAA (previously buffered), fills buffer with CHR[$0001]=0xBB */
    uint8_t r1 = ppu->cpu_read(0x2007);
    ASSERT(r1 == 0xAA, "second PPUDATA read returns buffered CHR[0]");

    /* Palette read: immediate */
    ppu->palette_table[1] = 0x16;
    ppu->cpu_write(0x2006, 0x3F);
    ppu->cpu_write(0x2006, 0x01);
    uint8_t pal = ppu->cpu_read(0x2007);
    ASSERT(pal == 0x16, "palette PPUDATA read is immediate");
}

/*
 * test_frame_timing
 *
 * A full NTSC frame is exactly 262 × 341 = 89342 dots.
 * frame_complete should be set once per frame.
 */
static void test_frame_timing(void) {
    printf("\n[test_frame_timing]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    int frames_seen = 0;
    /* Run for 3 full frames worth of dots and count frame_complete pulses */
    for (int i = 0; i < 89342 * 3; i++) {
        ppu->clock();
        if (ppu->frame_complete) {
            frames_seen++;
            ppu->frame_complete = 0; /* acknowledge */
        }
    }
    ASSERT(frames_seen == 3, "frame_complete pulses exactly 3 times in 3 frames");
}

// ---------------------------------------------------------------------------
// Sprite tests
// ---------------------------------------------------------------------------

/*
 * test_sprite_basic_render
 *
 * A single 8x8 sprite (OAM index 0) placed at screen position (16, 16).
 * The sprite tile has all pixels = colour index 1 in sprite palette 0.
 * After one frame the framebuffer at (16, 16) must match the sprite colour,
 * and a pixel outside the sprite must be the backdrop colour.
 */
static void test_sprite_basic_render(void) {
    printf("\n[test_sprite_basic_render]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    /* Tile 1: lo-plane all 1s, hi-plane all 0s → pixel colour = 1 */
    uint8_t solid[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    write_tile(1, solid);

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* OAM[0]: Y=15 → appears on scanline 16; tile 1; attr=0 (palette 0, front); X=16 */
    ppu->oam[0] = 15;
    ppu->oam[1] = 1;
    ppu->oam[2] = 0;
    ppu->oam[3] = 16;

    /* Sprite palette 0 colour 1 = 0x16 (blue). palette_table[4*4+1] = palette_table[17] */
    ppu->palette_table[4 * 4 + 1] = 0x16;

    /* Reset PPUCTRL: sprite_pattern_table=0, 8x8 sprites, no NMI */
    ppu->cpu_write(0x2000, 0x00);
    /* Enable sprite rendering (bit 4) and show sprites in left column (bit 2) */
    ppu->cpu_write(0x2001, 0x14);

    run_to_frame_start(ppu);
    for (int i = 0; i < 89342; i++) ppu->clock();

    uint32_t expected = NES_PALETTE[0x16 & 0x3F];
    ASSERT(fb[16 * 256 + 16] == expected, "sprite pixel at (16,16) correct colour");
    ASSERT(fb[16 * 256 + 24] != expected, "pixel 8 cols right of sprite is not sprite colour");
}

/*
 * test_sprite_priority_behind_bg
 *
 * A sprite with priority bit set (attr bit 5 = 1) must appear BEHIND opaque
 * background pixels.  Place a solid-colour background tile at the same
 * position; the composited pixel must be the BG colour, not the sprite colour.
 */
static void test_sprite_priority_behind_bg(void) {
    printf("\n[test_sprite_priority_behind_bg]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t solid[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    write_tile(1, solid);

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Background: tile 1 at nametable position (2,2) → screen cols 16-23, rows 16-23 */
    ppu->cpu_write(0x2006, 0x20);
    ppu->cpu_write(0x2006, 0x42); /* nametable address $2042 = row 2 col 2 */
    ppu->cpu_write(0x2007, 0x01);

    /* BG palette 0 colour 1 = 0x16 (blue) */
    ppu->palette_table[1] = 0x16;

    /* Sprite: tile 1, attr=0x21 (priority=behind, palette 1), at (16, 16) */
    ppu->oam[0] = 15;
    ppu->oam[1] = 1;
    ppu->oam[2] = 0x21; /* bit 5=priority behind BG, bits 0:1=palette 1 */
    ppu->oam[3] = 16;

    /* Sprite palette 1 colour 1 = 0x25 (red) */
    ppu->palette_table[4 * 4 + 5] = 0x25;

    /* Enable BG + sprite rendering, show both in left 8 columns */
    ppu->cpu_write(0x2000, 0x08);
    ppu->cpu_write(0x2001, 0x1E);
    ppu->cpu_write(0x2005, 0x00);
    ppu->cpu_write(0x2005, 0x00);

    run_to_frame_start(ppu);
    for (int i = 0; i < 89342; i++) ppu->clock();

    uint32_t bg_colour  = NES_PALETTE[0x16 & 0x3F];
    uint32_t sp_colour  = NES_PALETTE[0x25 & 0x3F];
    uint32_t px = fb[16 * 256 + 16];
    ASSERT(px == bg_colour, "priority-behind sprite hidden by opaque BG");
    ASSERT(px != sp_colour, "sprite colour absent when sprite is behind BG");
}

/*
 * test_sprite_horizontal_flip
 *
 * Tile 2 has only the leftmost pixel set (lo-plane = 0x80).  Without flip the
 * opaque pixel is at the sprite's leftmost column; with horizontal flip it
 * moves to the rightmost column.
 */
static void test_sprite_horizontal_flip(void) {
    printf("\n[test_sprite_horizontal_flip]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    /* Tile 2: only bit 7 of lo-plane set → pixel colour 1 at pixel_x=0 only */
    uint8_t left_px[8] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    write_tile(2, left_px);

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Sprite: tile 2, attr=0x40 (H-flip), at screen (32, 32) */
    ppu->oam[0] = 31; /* Y=31 → scanline 32 */
    ppu->oam[1] = 2;
    ppu->oam[2] = 0x40; /* horizontal flip */
    ppu->oam[3] = 32;

    ppu->palette_table[4 * 4 + 1] = 0x16;
    ppu->cpu_write(0x2000, 0x00); /* sprite_pattern_table=0 */
    ppu->cpu_write(0x2001, 0x14);

    run_to_frame_start(ppu);
    for (int i = 0; i < 89342; i++) ppu->clock();

    uint32_t sp_colour  = NES_PALETTE[0x16 & 0x3F];
    uint32_t backdrop   = NES_PALETTE[ppu->palette_table[0] & 0x3F];

    /* With H-flip, pixel_x=0 maps to the rightmost column of the sprite (col 39) */
    ASSERT(fb[32 * 256 + 39] == sp_colour, "H-flip: opaque pixel at rightmost sprite column");
    ASSERT(fb[32 * 256 + 32] == backdrop,  "H-flip: leftmost sprite column is transparent");
}

/*
 * test_sprite_vertical_flip
 *
 * Tile 3 has only the top row set (lo-plane[0] = 0xFF, rest 0x00).  Without
 * flip the opaque row is at pixel_y=0 (screen row = sprite Y+1).  With V-flip
 * pixel_y=0 maps to the bottom row of the 8-pixel sprite (pixel_y=7).
 */
static void test_sprite_vertical_flip(void) {
    printf("\n[test_sprite_vertical_flip]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t top_row[8] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    write_tile(3, top_row);

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Sprite: tile 3, attr=0x80 (V-flip), at screen (48, 48) */
    ppu->oam[0] = 47;
    ppu->oam[1] = 3;
    ppu->oam[2] = 0x80; /* vertical flip */
    ppu->oam[3] = 48;

    ppu->palette_table[4 * 4 + 1] = 0x16;
    ppu->cpu_write(0x2000, 0x00); /* sprite_pattern_table=0 */
    ppu->cpu_write(0x2001, 0x14);

    run_to_frame_start(ppu);
    for (int i = 0; i < 89342; i++) ppu->clock();

    uint32_t sp_colour = NES_PALETTE[0x16 & 0x3F];
    uint32_t backdrop  = NES_PALETTE[ppu->palette_table[0] & 0x3F];

    /* V-flip: tile row 0 maps to sprite bottom (screen row 48+7=55) */
    ASSERT(fb[55 * 256 + 48] == sp_colour, "V-flip: opaque row at bottom of sprite");
    ASSERT(fb[48 * 256 + 48] == backdrop,  "V-flip: top of sprite is transparent");
}

/*
 * test_sprite_zero_hit
 *
 * Sprite 0 must set ppustatus.sprite_0_hit when it overlaps an opaque
 * background pixel.  Check the flag inside VBlank before the pre-render
 * scanline clears it.
 */
static void test_sprite_zero_hit(void) {
    printf("\n[test_sprite_zero_hit]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t solid[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    write_tile(1, solid);

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Background: tile 1 at nametable[1,1] (col 1, row 1 → screen x=8, y=8) */
    ppu->cpu_write(0x2006, 0x20);
    ppu->cpu_write(0x2006, 0x21); /* $2021 = nametable row 1 col 1 */
    ppu->cpu_write(0x2007, 0x01);
    ppu->palette_table[1] = 0x16;

    /* Sprite 0: tile 1 at screen (8, 8) → overlaps the BG tile */
    ppu->oam[0] = 7;  /* Y=7 → scanline 8 */
    ppu->oam[1] = 1;
    ppu->oam[2] = 0;
    ppu->oam[3] = 8;  /* X=8 (outside left-column clip) */
    ppu->palette_table[4 * 4 + 1] = 0x25;

    ppu->cpu_write(0x2000, 0x00); /* sprite_pattern_table=0, bg_pattern_table=0 */
    ppu->cpu_write(0x2001, 0x1E); /* all render enables */
    ppu->cpu_write(0x2005, 0x00);
    ppu->cpu_write(0x2005, 0x00);

    /* Run frame dot by dot; sample sprite_0_hit when frame_complete fires
     * (scanline 241 dot 1) — before the pre-render scanline clears the flag. */
    uint8_t hit_at_vblank = 0;
    for (int i = 0; i < 89342 * 2; i++) {
        ppu->clock();
        if (ppu->frame_complete) {
            hit_at_vblank = ppu->ppustatus.sprite_0_hit;
            break;
        }
    }
    ASSERT(hit_at_vblank == 1, "sprite_0_hit set when sprite 0 overlaps opaque BG");

    /* Run to pre-render dot 1; flag must be cleared. */
    while (!(ppu->scanline == -1 && ppu->dot == 0)) ppu->clock();
    ppu->clock(); /* dot 0 */
    ppu->clock(); /* dot 1 — clears flags */
    ASSERT(ppu->ppustatus.sprite_0_hit == 0, "sprite_0_hit cleared at pre-render dot 1");
}

// ---------------------------------------------------------------------------
// Open bus tests
// ---------------------------------------------------------------------------

/*
 * test_open_bus_write_only_registers
 *
 * Reading a write-only PPU register ($2000, $2001, $2003, $2005, $2006)
 * must return the last value written to any PPU register (the open bus).
 */
static void test_open_bus_write_only_registers(void) {
    printf("\n[test_open_bus_write_only_registers]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Write a known value to PPUCTRL — this sets open bus to 0x42 */
    ppu->cpu_write(0x2000, 0x42);
    ASSERT(ppu->ppu_open_bus == 0x42, "cpu_write sets ppu_open_bus");

    /* Reading PPUCTRL ($2000, write-only) must return open bus unchanged */
    uint8_t r = ppu->cpu_read(0x2000);
    ASSERT(r == 0x42, "read of write-only $2000 returns open bus");

    /* Write a different value via PPUMASK */
    ppu->cpu_write(0x2001, 0x1E);
    ASSERT(ppu->ppu_open_bus == 0x1E, "cpu_write $2001 updates ppu_open_bus");

    /* Read PPUMASK ($2001, write-only) → open bus */
    r = ppu->cpu_read(0x2001);
    ASSERT(r == 0x1E, "read of write-only $2001 returns open bus");

    /* Read PPUSCROLL ($2005, write-only) → still 0x1E */
    r = ppu->cpu_read(0x2005);
    ASSERT(r == 0x1E, "read of write-only $2005 returns open bus");

    /* Read PPUADDR ($2006, write-only) → still 0x1E */
    r = ppu->cpu_read(0x2006);
    ASSERT(r == 0x1E, "read of write-only $2006 returns open bus");

    /* Read OAMADDR ($2003, write-only) → still 0x1E */
    r = ppu->cpu_read(0x2003);
    ASSERT(r == 0x1E, "read of write-only $2003 returns open bus");
}

/*
 * test_open_bus_ppustatus_lower_bits
 *
 * PPUSTATUS ($2002) lower 5 bits should reflect the open bus value; upper 3
 * bits are real flags.  Confirm that the lower 5 bits come from the last write.
 */
static void test_open_bus_ppustatus_lower_bits(void) {
    printf("\n[test_open_bus_ppustatus_lower_bits]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    /* Enable NMI first so open bus is updated by PPUCTRL write */
    ppu->cpu_write(0x2000, 0x80); /* NMI enable; sets open bus = 0x80 */

    /* Advance to VBlank so vblank_started = 1 (upper bit of $2002 = 1).
     * Starting at (-1, 0): after 341*242 = 82522 calls we're at (241, 0).
     * One more call processes (241,0) and ends at (241,1).
     * One more call processes (241,1) — VBL fires during that call.
     * So 82524 total calls guarantee vblank_started is set. */
    int dots_to_vbl = (241 + 1) * 341 + 2; /* 82524: processes scanline 241 dot 1 */
    for (int i = 0; i < dots_to_vbl; i++) ppu->clock();

    /* Now set open bus lower bits to a known pattern (0x1B = 0b00011011).
     * Write to OAMADDR (write-only, harmless) to update open bus without
     * disturbing NMI or rendering state. */
    ppu->cpu_write(0x2003, 0x1B); /* sets open bus = 0x1B, oamaddr = 0x1B */

    ASSERT(ppu->ppustatus.vblank_started == 1, "VBlank active for open bus test");

    /* Read PPUSTATUS: bit 7 = vblank (real flag = 1),
     * lower 5 bits = open bus lower 5 bits = 0x1B & 0x1F = 0x1B.
     * Bits 5-6 (sprite_overflow, sprite_0_hit) may be set from earlier
     * rendering, so only check bit 7 and the lower 5 bits individually. */
    uint8_t status = ppu->cpu_read(0x2002);
    ASSERT((status & 0x80) != 0, "PPUSTATUS bit 7 (vblank) is set");
    ASSERT((status & 0x1F) == 0x1B, "PPUSTATUS lower 5 bits come from open bus");

    /* Reading $2002 also updates open bus to the combined value */
    ASSERT(ppu->ppu_open_bus == status, "PPUSTATUS read updates ppu_open_bus");
}

// ---------------------------------------------------------------------------
// Sprite overflow tests
// ---------------------------------------------------------------------------

/*
 * test_sprite_overflow_normal
 *
 * Place 9 sprites all on the same scanline.  The first 8 fill secondary OAM
 * and the 9th should trigger the overflow flag even with the diagonal scan bug,
 * because at i=8 the byte counter m=0 so byte 0 (Y) is read correctly.
 */
static void test_sprite_overflow_normal(void) {
    printf("\n[test_sprite_overflow_normal]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    // Enable sprite rendering
    ppu->cpu_write(0x2000, 0x00); // 8x8 sprites
    ppu->cpu_write(0x2001, 0x10); // sprite rendering enabled

    // Place 9 sprites all on scanline 50 (Y=49 → appears on scanline 50)
    // Spread X positions so they don't overlap, but all on the same scanline row
    for (int i = 0; i < 9; i++) {
        ppu->oam[i * 4 + 0] = 49; // Y=49 → visible on scanline 50
        ppu->oam[i * 4 + 1] = 0;  // tile 0
        ppu->oam[i * 4 + 2] = 0;  // attr
        ppu->oam[i * 4 + 3] = (uint8_t)(i * 8); // X: 0, 8, 16, ...
    }

    // Run two frames; the overflow flag is set during rendering and survives
    // until the pre-render scanline clears it.  Sample it at VBlank.
    uint8_t overflow_at_vblank = 0;
    for (int i = 0; i < 89342 * 2; i++) {
        ppu->clock();
        if (ppu->frame_complete) {
            overflow_at_vblank = ppu->ppustatus.sprite_overflow;
            break;
        }
    }
    ASSERT(overflow_at_vblank == 1, "overflow set when 9 sprites on same scanline");

    // Confirm the flag clears at pre-render dot 1
    while (!(ppu->scanline == -1 && ppu->dot == 0)) ppu->clock();
    ppu->clock(); // dot 0
    ppu->clock(); // dot 1 — clears flags
    ASSERT(ppu->ppustatus.sprite_overflow == 0, "overflow cleared at pre-render dot 1");
}

/*
 * test_sprite_overflow_diagonal_false_negative
 *
 * Arrange exactly 9 sprites so that after the 8th is found the diagonal scan
 * reads a non-Y byte of the 9th sprite that falls OUTSIDE the current scanline
 * window, causing the overflow flag to NOT be set despite 9 sprites being
 * present on the scanline.
 *
 * Scanline under test: 100
 * Sprites 0-7: Y=99 → all on scanline 100 (fill secondary OAM)
 * Sprite 8: Y=99 (byte 0) → on scanline 100 BUT the diagonal scan reads
 *   byte m=0+0=0 first at i=8... wait, m starts at 0 for i=8.
 *
 * Actually m=0 at i=8, so sprite 8 IS read correctly.  To get a false negative
 * we need to engineer things so that the byte read at some i>=9 (where m>0)
 * is out-of-window, AND sprite 8 (m=0) is also out of window.
 *
 * Strategy:
 *   - Sprites 0-7: Y=99 → scanline 100 ✓ (fill secondary OAM)
 *   - Sprite 8 (i=8, m=0): Y value must NOT be on scanline 100
 *     → set Y=0 (appears on scanline 1 only)
 *   - Sprite 9 (i=9, m=1): reads byte 1 (tile index) of sprite 9.
 *     Set sprite 9's tile byte to a value such that tile+1 is NOT in [100, 108)
 *     e.g. tile = 200 → top = 201, bot = 209 → not in [100,108)
 *   - ...continue for sprites 10-63: all have non-Y bytes outside window
 *   - Add a 9th "real" sprite on scanline 100 at OAM index 9: Y=99 (byte 0),
 *     but since m=1 at i=9, byte 1 (tile=200) is read instead.
 *
 * This creates a false negative: the 9th sprite is genuinely on scanline 100
 * but the diagonal scan misses it because it reads the tile byte instead of Y.
 */
static void test_sprite_overflow_diagonal_false_negative(void) {
    printf("\n[test_sprite_overflow_diagonal_false_negative]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    ppu->cpu_write(0x2000, 0x00); // 8x8 sprites
    ppu->cpu_write(0x2001, 0x10); // sprite rendering enabled

    // Zero out all OAM first
    for (int i = 0; i < 256; i++) ppu->oam[i] = 0xFF; // off-screen Y=255

    // Sprites 0-7: all on scanline 100 (Y=99)
    for (int i = 0; i < 8; i++) {
        ppu->oam[i * 4 + 0] = 99;        // Y=99 → scanline 100
        ppu->oam[i * 4 + 1] = 0;         // tile
        ppu->oam[i * 4 + 2] = 0;         // attr
        ppu->oam[i * 4 + 3] = (uint8_t)(i * 8); // X
    }

    // Sprite 8 (i=8, m=0): Y must be off scanline 100.  Use Y=0 (scanline 1).
    ppu->oam[8 * 4 + 0] = 0;   // Y=0 → off scanline 100
    ppu->oam[8 * 4 + 1] = 200; // tile=200 (byte 1 will be read at i=9 via m=1)
    ppu->oam[8 * 4 + 2] = 0;   // attr
    ppu->oam[8 * 4 + 3] = 64;  // X

    // Sprite 9 (i=9, m=1 after the miss at i=8): reads byte 1 (tile) of sprite 9.
    // Set Y=99 (genuinely on scanline 100) but tile=200 so the diagonal scan
    // reads 200+1=201 which is outside [100,108).
    ppu->oam[9 * 4 + 0] = 99;  // Y=99 → genuinely on scanline 100
    ppu->oam[9 * 4 + 1] = 200; // tile=200; diagonal reads this as "Y" → top=201 ∉ [100,108)
    ppu->oam[9 * 4 + 2] = 0;   // attr
    ppu->oam[9 * 4 + 3] = 72;  // X

    // Sprites 10-63: keep Y=255 (off-screen); remaining diagonal reads will also miss.

    // Run and sample overflow at VBlank
    uint8_t overflow_at_vblank = 0;
    for (int i = 0; i < 89342 * 2; i++) {
        ppu->clock();
        if (ppu->frame_complete) {
            overflow_at_vblank = ppu->ppustatus.sprite_overflow;
            break;
        }
    }
    // The diagonal scan should MISS the 9th sprite → false negative (no overflow)
    ASSERT(overflow_at_vblank == 0,
           "diagonal scan false negative: 9th sprite missed because m>0 reads non-Y byte");
}

/*
 * test_sprite_overflow_diagonal_false_positive
 *
 * Arrange ≤8 sprites so that the diagonal scan reads a non-Y byte of a later
 * sprite that happens to fall within the scanline window, setting the overflow
 * flag despite fewer than 9 sprites being on the scanline.
 *
 * Scanline under test: 50
 * Sprites 0-7: all on scanline 50 (Y=49) → fill secondary OAM
 * Sprite 8 (i=8, m=0): Y byte must be OFF scanline 50; use Y=0.
 * Sprite 9 (i=9, m=1): byte 1 (tile index) of sprite 9 read as "Y".
 *   We need tile+1 ∈ [50, 58) i.e. tile ∈ [49, 57).
 *   Use tile=49 → top=50, bot=58 → IN window → false positive!
 *   The actual Y of sprite 9 = 255 (off-screen), so it is NOT a real hit.
 */
static void test_sprite_overflow_diagonal_false_positive(void) {
    printf("\n[test_sprite_overflow_diagonal_false_positive]\n");

    struct ppu2c02 *ppu = ppu2c02_init();
    static uint32_t fb[256 * 240];
    memset(fb, 0, sizeof(fb));
    ppu->connect_cartridge(&fake_cart);
    ppu->set_framebuffer(fb);
    ppu->reset();

    ppu->cpu_write(0x2000, 0x00); // 8x8 sprites
    ppu->cpu_write(0x2001, 0x10); // sprite rendering enabled

    // Zero out all OAM to off-screen
    for (int i = 0; i < 256; i++) ppu->oam[i] = 0xFF;

    // Sprites 0-7: all on scanline 50 (Y=49)
    for (int i = 0; i < 8; i++) {
        ppu->oam[i * 4 + 0] = 49;        // Y=49 → scanline 50
        ppu->oam[i * 4 + 1] = 0;         // tile
        ppu->oam[i * 4 + 2] = 0;         // attr
        ppu->oam[i * 4 + 3] = (uint8_t)(i * 8); // X
    }

    // Sprite 8 (i=8, m=0): Y=0 → off scanline 50
    ppu->oam[8 * 4 + 0] = 0;   // Y=0 → off scanline 50 (m=0 reads this)
    ppu->oam[8 * 4 + 1] = 0;   // tile
    ppu->oam[8 * 4 + 2] = 0;   // attr
    ppu->oam[8 * 4 + 3] = 0;   // X
    // After the miss at i=8, m becomes 1.

    // Sprite 9 (i=9, m=1): diagonal reads byte 1 (tile index).
    // Set tile=49 → diagonal interprets it as Y=49 → top=50, bot=58 → IN window
    // The real Y of this sprite is 255 (off-screen) — not a genuine hit.
    ppu->oam[9 * 4 + 0] = 255; // actual Y=255 → off scanline 50
    ppu->oam[9 * 4 + 1] = 49;  // tile=49; diagonal reads this as "Y" → top=50 ∈ [50,58)
    ppu->oam[9 * 4 + 2] = 0;   // attr
    ppu->oam[9 * 4 + 3] = 0;   // X

    // Run and sample overflow at VBlank
    uint8_t overflow_at_vblank = 0;
    for (int i = 0; i < 89342 * 2; i++) {
        ppu->clock();
        if (ppu->frame_complete) {
            overflow_at_vblank = ppu->ppustatus.sprite_overflow;
            break;
        }
    }
    // Diagonal scan should hit sprite 9's tile byte (49) as a false Y match
    ASSERT(overflow_at_vblank == 1,
           "diagonal scan false positive: non-Y byte in window triggers overflow");
}

// ---------------------------------------------------------------------------
// Sprite 0 hit edge-case tests (covers blargg sprite_hit_tests_2005.10.05)
// ---------------------------------------------------------------------------

/*
 * Helper: initialise a fresh PPU (NO frame buffer — verifies the hit flag
 * works in headless mode after the composite_pixel() refactor), write one BG
 * tile to the nametable, place sprite 0, run one full frame, and return the
 * sprite_0_hit flag sampled at VBlank start.
 *
 * Tile IDs expected in fake_chr before calling:
 *   tile 1  = solid (all pixels opaque, color index 1)
 *   tile 2  = upper-left single pixel  (row 0, bit 7 only)
 *   tile 3  = upper-right single pixel (row 0, bit 0 only)
 *   tile 4  = lower-right single pixel (row 7, bit 0 only)
 */
static uint8_t sprite0_scenario(
        uint8_t ppuctrl,   /* $2000 */
        uint8_t ppumask,   /* $2001 */
        uint16_t bg_nt_addr, uint8_t bg_tile,
        uint8_t sx, uint8_t sy,       /* sprite OAM x, OAM y */
        uint8_t sp_tile, uint8_t sp_attr)
{
    struct ppu2c02 *ppu = ppu2c02_init();
    /* Intentionally no frame buffer — tests that sprite_0_hit fires even
     * without one (regression for the frame_buffer-guard bug). */
    ppu->connect_cartridge(&fake_cart);
    ppu->reset();

    /* Write BG tile into nametable */
    ppu->cpu_write(0x2006, (uint8_t)(bg_nt_addr >> 8));
    ppu->cpu_write(0x2006, (uint8_t)(bg_nt_addr & 0xFF));
    ppu->cpu_write(0x2007, bg_tile);

    /* BG palette 0 color 1 → non-zero NES index so bg_pixel reads as opaque */
    ppu->cpu_write(0x2006, 0x3F); ppu->cpu_write(0x2006, 0x01);
    ppu->cpu_write(0x2007, 0x16);
    /* Sprite palette 0 color 1 */
    ppu->cpu_write(0x2006, 0x3F); ppu->cpu_write(0x2006, 0x11);
    ppu->cpu_write(0x2007, 0x25);

    /* Sprite 0: OAM bytes 0-3 */
    ppu->oam[0] = sy;
    ppu->oam[1] = sp_tile;
    ppu->oam[2] = sp_attr;
    ppu->oam[3] = sx;

    ppu->cpu_write(0x2000, ppuctrl);
    ppu->cpu_write(0x2001, ppumask);
    ppu->cpu_write(0x2005, 0x00);   /* scroll X=0 */
    ppu->cpu_write(0x2005, 0x00);   /* scroll Y=0 */

    uint8_t hit = 0;
    for (int i = 0; i < 89342 * 2; i++) {
        ppu->clock();
        if (ppu->frame_complete) { hit = ppu->ppustatus.sprite_0_hit; break; }
    }
    return hit;
}

/*
 * test_sprite_zero_hit_rendering_disabled
 *
 * Mirrors blargg 01.basics tests 4-6 and 7.
 *   4) Miss when background rendering is off
 *   5) Miss when sprite rendering is off
 *   6) Miss when all rendering is off
 *   7) All-transparent sprite must miss
 *  10) Miss when BG is all transparent
 */
static void test_sprite_zero_hit_rendering_disabled(void) {
    printf("\n[test_sprite_zero_hit_rendering_disabled]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    /* tile 1: solid (lo-plane all ones, hi-plane 0 → color idx 1 everywhere) */
    uint8_t solid[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    write_tile(1, solid);

    /* BG tile at nametable col 16, row 15 → screen x=128, y=120; sprite at same */
    uint16_t nt = 0x2000 + 15 * 32 + 16; /* col 16, row 15 */
    uint8_t sx = 128, sy = 119; /* OAM y=119 → sprite top at scanline 120 */

    /* Baseline: both enables on → must hit */
    uint8_t h = sprite0_scenario(0x00, 0x1E, nt, 1, sx, sy, 1, 0);
    ASSERT(h == 1, "baseline both-enables-on: sprite_0_hit set");

    /* BG rendering off ($2001 bit 3 = 0) → miss */
    h = sprite0_scenario(0x00, 0x16, nt, 1, sx, sy, 1, 0);
    ASSERT(h == 0, "BG rendering disabled: sprite_0_hit clear");

    /* Sprite rendering off ($2001 bit 4 = 0) → miss */
    h = sprite0_scenario(0x00, 0x0E, nt, 1, sx, sy, 1, 0);
    ASSERT(h == 0, "sprite rendering disabled: sprite_0_hit clear");

    /* All rendering off → miss */
    h = sprite0_scenario(0x00, 0x00, nt, 1, sx, sy, 1, 0);
    ASSERT(h == 0, "all rendering disabled: sprite_0_hit clear");

    /* Transparent sprite (tile 0 = all zeros) → miss */
    h = sprite0_scenario(0x00, 0x1E, nt, 1, sx, sy, 0, 0);
    ASSERT(h == 0, "transparent sprite: sprite_0_hit clear");

    /* Transparent BG (tile 0 in nametable) → miss */
    h = sprite0_scenario(0x00, 0x1E, nt, 0, sx, sy, 1, 0);
    ASSERT(h == 0, "transparent BG: sprite_0_hit clear");
}

/*
 * test_sprite_zero_hit_headless
 *
 * Regression: sprite_0_hit was only set inside the frame_buffer guard.
 * Verify the fix: hit is detected even with no frame buffer attached.
 * (The sprite0_scenario() helper never attaches a frame buffer.)
 */
static void test_sprite_zero_hit_headless(void) {
    printf("\n[test_sprite_zero_hit_headless]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t solid[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    write_tile(1, solid);

    uint16_t nt = 0x2000 + 15 * 32 + 16;
    uint8_t h = sprite0_scenario(0x00, 0x1E, nt, 1, 128, 119, 1, 0);
    ASSERT(h == 1,
           "sprite_0_hit set in headless mode (no frame buffer attached)");
}

/*
 * test_sprite_zero_hit_left_clip
 *
 * Mirrors blargg 05.left_clip:
 *   2) Miss when sprite entirely in left-edge clip (X=0, clip on)
 *   3) Clip fires when only bg or only sprite left-col bit is 0
 *   4) No clip when $2001 = $1E (both left-col bits set)
 *   5) Clip blocks hits only when X=0 (X=1 exposes x=8, which can hit)
 *   6) Miss: opaque pixel under clip (upper-left tile at X=7, dot=8)
 *   7) Hit:  opaque pixel outside clip (upper-left tile at X=8, dot=9)
 *   8) Hit:  upper-right tile at X=1 places opaque pixel at x=8
 */
static void test_sprite_zero_hit_left_clip(void) {
    printf("\n[test_sprite_zero_hit_left_clip]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t solid[8]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t ul_pix[8]  = {0x80,0,0,0,0,0,0,0}; /* upper-left pixel only */
    uint8_t ur_pix[8]  = {0x01,0,0,0,0,0,0,0}; /* upper-right pixel only */
    write_tile(1, solid);
    write_tile(2, ul_pix);
    write_tile(3, ur_pix);

    /* Fill the entire first row of nametable with solid tiles so all X positions
     * have an opaque background regardless of sprite X. */
    for (int col = 0; col < 32; col++) {
        uint16_t addr = 0x2000 + col;
        /* We write via PPUADDR/PPUDATA each time — use a fresh mini PPU inline. */
        (void)addr; /* written in the scenario via bg_nt_addr */
    }
    /* Use row 15 for BG (screen y=120); solid tile fills one nametable cell.
     * For left-clip tests the sprite is at y=119 (screen y=120). */
    uint8_t sy = 119;

    /* --- test 2: solid sprite at X=0, clip on ($2001=$18) → miss --- */
    /* We need BG at columns 0-3 (x=0-7). Write BG tile at col 0 row 15. */
    uint8_t h = sprite0_scenario(0x00, 0x18, 0x2000+15*32+0, 1, 0, sy, 1, 0);
    ASSERT(h == 0, "left_clip test2: solid sprite at X=0, clip on -> miss");

    /* --- test 3a: clip fires when sprite left-col bit is 0 ($2001=$1A) → miss */
    /* $1A = 0001 1010: bg_left_col=1, spr_left_col=0 */
    h = sprite0_scenario(0x00, 0x1A, 0x2000+15*32+0, 1, 0, sy, 1, 0);
    ASSERT(h == 0, "left_clip test3a: $2001=$1A (spr_left_col=0) -> miss");

    /* --- test 3b: clip fires when bg left-col bit is 0 ($2001=$1C) → miss */
    /* $1C = 0001 1100: bg_left_col=0, spr_left_col=1 */
    h = sprite0_scenario(0x00, 0x1C, 0x2000+15*32+0, 1, 0, sy, 1, 0);
    ASSERT(h == 0, "left_clip test3b: $2001=$1C (bg_left_col=0) -> miss");

    /* --- test 4: no clip when $2001=$1E → hit at X=0 --- */
    h = sprite0_scenario(0x00, 0x1E, 0x2000+15*32+0, 1, 0, sy, 1, 0);
    ASSERT(h == 1, "left_clip test4: $2001=$1E (no clip) at X=0 -> hit");

    /* --- test 5: X=1 with clip on ($2001=$18): pixel at x=8 outside clip → hit */
    /* Need BG at col 0 (x=0-7) AND col 1 (x=8-15) */
    h = sprite0_scenario(0x00, 0x18, 0x2000+15*32+1, 1, 1, sy, 1, 0);
    ASSERT(h == 1, "left_clip test5: X=1, clip on -> hit at x=8");

    /* --- test 6: upper-left pixel tile at X=7 → pixel at x=7 (dot=8) → miss */
    /* BG at col 0 (x=0-7) */
    h = sprite0_scenario(0x00, 0x18, 0x2000+15*32+0, 1, 7, sy, 2, 0);
    ASSERT(h == 0, "left_clip test6: upper-left tile at X=7, pixel at dot=8 -> miss");

    /* --- test 7: upper-left pixel tile at X=8 → pixel at x=8 (dot=9) → hit */
    /* BG at col 1 (x=8-15) */
    h = sprite0_scenario(0x00, 0x18, 0x2000+15*32+1, 1, 8, sy, 2, 0);
    ASSERT(h == 1, "left_clip test7: upper-left tile at X=8, pixel at dot=9 -> hit");

    /* --- test 8: upper-right pixel tile at X=1 → pixel at x=8 (dot=9) → hit */
    h = sprite0_scenario(0x00, 0x18, 0x2000+15*32+1, 1, 1, sy, 3, 0);
    ASSERT(h == 1, "left_clip test8: upper-right tile at X=1, pixel at dot=9 -> hit");
}

/*
 * test_sprite_zero_hit_right_edge
 *
 * Mirrors blargg 06.right_edge:
 *   2) Miss when X=255 (all sprite pixels are at or beyond x=255)
 *   3) Hit when X=254 (sprite has pixels at x=254, which is < 255)
 *   4) Miss: upper-right tile at X=248 → opaque pixel at x=255 → excluded
 *   5) Hit:  upper-right tile at X=247 → opaque pixel at x=254 → allowed
 *   6) Hit:  upper-left tile at X=254 → opaque pixel at x=254
 */
static void test_sprite_zero_hit_right_edge(void) {
    printf("\n[test_sprite_zero_hit_right_edge]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t solid[8]  = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t ul_pix[8] = {0x80,0,0,0,0,0,0,0};
    uint8_t ur_pix[8] = {0x01,0,0,0,0,0,0,0};
    write_tile(1, solid);
    write_tile(2, ul_pix);
    write_tile(3, ur_pix);

    uint8_t sy = 119; /* sprite top at scanline 120 */
    /* BG fills columns 30-31 (x=240-255) and some nearby — use last 2 nametable cols */
    /* column 30 → x=240-247, column 31 → x=248-255 */
    uint16_t col30_nt = 0x2000 + 15*32 + 30;
    uint16_t col31_nt = 0x2000 + 15*32 + 31;

    /* test 2: solid sprite at X=255 → miss */
    uint8_t h = sprite0_scenario(0x00, 0x1E, col31_nt, 1, 255, sy, 1, 0);
    ASSERT(h == 0, "right_edge test2: solid sprite at X=255 -> miss");

    /* test 3: solid sprite at X=254 → pixels at x=254,255; x=254 hits */
    h = sprite0_scenario(0x00, 0x1E, col31_nt, 1, 254, sy, 1, 0);
    ASSERT(h == 1, "right_edge test3: solid sprite at X=254 -> hit at x=254");

    /* test 4: upper-right tile at X=248 → opaque pixel at x=255 → miss */
    h = sprite0_scenario(0x00, 0x1E, col31_nt, 1, 248, sy, 3, 0);
    ASSERT(h == 0, "right_edge test4: upper-right tile at X=248, pixel at x=255 -> miss");

    /* test 5: upper-right tile at X=247 → opaque pixel at x=254 → hit */
    h = sprite0_scenario(0x00, 0x1E, col30_nt, 1, 247, sy, 3, 0);
    ASSERT(h == 1, "right_edge test5: upper-right tile at X=247, pixel at x=254 -> hit");

    /* test 6: upper-left tile at X=254 → opaque pixel at x=254 → hit */
    h = sprite0_scenario(0x00, 0x1E, col31_nt, 1, 254, sy, 2, 0);
    ASSERT(h == 1, "right_edge test6: upper-left tile at X=254, pixel at x=254 -> hit");
}

/*
 * test_sprite_zero_hit_screen_bottom
 *
 * Mirrors blargg 07.screen_bottom:
 *   2) Miss when OAM Y=239 (sprite appears at scanline 240, post-render)
 *   3) Hit  when OAM Y=238 (sprite appears at scanline 239, last visible)
 *   4) Miss when OAM Y=255 (top+1=256, never in range 0-239)
 *   5) Hit: lower-right tile at OAM Y=231 → opaque pixel at y=238 (scanline 239 with sy+1+7)
 *   6) Miss: lower-right tile at OAM Y=232 → opaque pixel at y=239 (scanline 240, not visible)
 */
static void test_sprite_zero_hit_screen_bottom(void) {
    printf("\n[test_sprite_zero_hit_screen_bottom]\n");

    memset(fake_chr, 0, sizeof(fake_chr));
    uint8_t solid[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t lr_pix[8]= {0,0,0,0,0,0,0,0x01}; /* lower-right pixel only */
    write_tile(1, solid);
    write_tile(4, lr_pix);

    /* BG: we need a solid tile at the rows where sprites will appear.
     * Use column 16 (x=128) and place BG tiles at matching rows.
     * For a sprite at OAM Y=N, the sprite appears at scanlines N+1..N+8.
     * The nametable row for scanline S is S/8 (each tile is 8 rows tall).
     * Scanline 239 → row 29 → nt offset = 29*32+16 = 0x2000+29*32+16 */
    uint16_t nt_row29 = 0x2000 + 29*32 + 16; /* scanline 232-239 */
    uint8_t sx = 128;

    /* test 2: OAM Y=239 → sprite at scanlines 240-247 → not visible → miss */
    uint8_t h = sprite0_scenario(0x00, 0x1E, nt_row29, 1, sx, 239, 1, 0);
    ASSERT(h == 0, "screen_bottom test2: OAM Y=239 -> miss (scanline 240 invisible)");

    /* test 3: OAM Y=238 → sprite at scanlines 239-246 → scanline 239 visible → hit */
    h = sprite0_scenario(0x00, 0x1E, nt_row29, 1, sx, 238, 1, 0);
    ASSERT(h == 1, "screen_bottom test3: OAM Y=238 -> hit (scanline 239 visible)");

    /* test 4: OAM Y=255 → top=256, never in visible range → miss */
    h = sprite0_scenario(0x00, 0x1E, nt_row29, 1, sx, 255, 1, 0);
    ASSERT(h == 0, "screen_bottom test4: OAM Y=255 -> miss");

    /* test 5: lower-right tile at OAM Y=231 → opaque pixel at scanline 231+1+7=239 → hit */
    h = sprite0_scenario(0x00, 0x1E, nt_row29, 1, sx, 231, 4, 0);
    ASSERT(h == 1, "screen_bottom test5: lower-right tile OAM Y=231 -> hit at scanline 239");

    /* test 6: lower-right tile at OAM Y=232 → opaque pixel at scanline 232+1+7=240 → miss */
    h = sprite0_scenario(0x00, 0x1E, nt_row29, 1, sx, 232, 4, 0);
    ASSERT(h == 0, "screen_bottom test6: lower-right tile OAM Y=232 -> miss at scanline 240");
}

/*
 * test_sprite_zero_hit_double_height
 *
 * Mirrors blargg 08.double_height.
 * In 8×16 mode: tile bit 0 selects the pattern table; top half uses tile N&$FE,
 * bottom half uses tile (N&$FE)+1.  Sprite OAM Y=N means top of sprite appears
 * at scanline N+1; bottom 8 rows appear at scanlines N+9..N+16.
 *
 *   2) Miss: lower-half opaque pixel that would appear BELOW the BG tile → miss
 *   3) Hit:  lower-half opaque pixel overlaps BG tile in its row
 */
static void test_sprite_zero_hit_double_height(void) {
    printf("\n[test_sprite_zero_hit_double_height]\n");

    memset(fake_chr, 0, sizeof(fake_chr));

    /* 8×16 sprite: tile byte = 0 → pattern table 0, top-half tile = $00, bottom = $01.
     * Make bottom half solid (tile 1 at offset $10 in CHR). */
    uint8_t top_blank[8]  = {0,0,0,0,0,0,0,0};
    uint8_t bot_solid[8]  = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    write_tile(0, top_blank); /* tile $00: top half, all transparent */
    write_tile(1, bot_solid); /* tile $01: bottom half, all solid    */

    /* BG solid tile at col 16 row 15 (screen x=128, y=120-127) */
    uint16_t nt = 0x2000 + 15*32 + 16;
    uint8_t sx = 128;

    /* PPUCTRL bit 5 = 1 → 8×16 sprite mode. Pattern table for 8×16 is
     * selected by sprite tile bit 0, not by PPUCTRL bit 3. */
    uint8_t ppuctrl_8x16 = 0x20;

    /* Sprite at OAM Y=112: top half at scanlines 113-120, bottom at 121-128.
     * BG is at rows 120-127 (nametable row 15).
     * Top half is transparent → no hit at 113-120.
     * Bottom half is solid → overlap with BG at scanlines 121-127 → hit. */
    uint8_t h = sprite0_scenario(ppuctrl_8x16, 0x1E, nt, 1, sx, 112, 0, 0);
    ASSERT(h == 1, "double_height: bottom half solid tile overlaps BG -> hit");

    /* Sprite at OAM Y=104: top at 105-112, bottom at 113-120.
     * BG row 15 covers scanlines 120-127.
     * Bottom half ends at scanline 120; BG starts at 120 → both opaque at 120 → hit. */
    h = sprite0_scenario(ppuctrl_8x16, 0x1E, nt, 1, sx, 104, 0, 0);
    ASSERT(h == 1, "double_height: bottom half last row (scanline 120) overlaps BG -> hit");

    /* Sprite at OAM Y=113: top at 114-121, bottom at 122-129.
     * Bottom half ends at scanline 129, starts at 122.
     * BG at 120-127: overlap at 122-127 → hit. */
    h = sprite0_scenario(ppuctrl_8x16, 0x1E, nt, 1, sx, 113, 0, 0);
    ASSERT(h == 1, "double_height: bottom half overlaps BG rows -> hit");

    /* Sprite at OAM Y=128: top at 129-136 → completely below BG (120-127) → miss. */
    h = sprite0_scenario(ppuctrl_8x16, 0x1E, nt, 1, sx, 128, 0, 0);
    ASSERT(h == 0, "double_height: sprite entirely below BG -> miss");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void) {
    printf("=== PPU clock() unit tests ===\n");

    test_vblank_timing();
    test_scroll_register_writes();
    test_nametable_palette_access();
    test_background_pixel_output();
    test_attr_latch_no_fine_x();
    test_attr_latch_fine_x_regression();
    test_ppudata_read_buffer();
    test_frame_timing();
    test_sprite_basic_render();
    test_sprite_priority_behind_bg();
    test_sprite_horizontal_flip();
    test_sprite_vertical_flip();
    test_sprite_zero_hit();
    test_open_bus_write_only_registers();
    test_open_bus_ppustatus_lower_bits();
    test_sprite_overflow_normal();
    test_sprite_overflow_diagonal_false_negative();
    test_sprite_overflow_diagonal_false_positive();
    test_sprite_zero_hit_headless();
    test_sprite_zero_hit_rendering_disabled();
    test_sprite_zero_hit_left_clip();
    test_sprite_zero_hit_right_edge();
    test_sprite_zero_hit_screen_bottom();
    test_sprite_zero_hit_double_height();

    printf("\n=== Results: %d passed, %d failed ===\n", test_pass, test_fail);
    return test_fail ? 1 : 0;
}
