// Mapper 005 — MMC5 (ExROM)
//
// One of the most complex NES mappers, used by ~25 licensed titles.
// Key compatible games: Castlevania III, Just Breed, Laser Invasion, Metal Slader Glory.
//
// ─── PRG banking ──────────────────────────────────────────────────────────────
//   $5100 PRG mode (bits 1-0):
//     0 = one 32 KB ROM window at $8000-$FFFF            ($5117 selects bank)
//     1 = two 16 KB windows:                              ($5115 / $5117)
//     2 = one 16 KB + two 8 KB windows:                  ($5115 / $5116 / $5117)
//     3 = four 8 KB windows (default):                   ($5114 / $5115 / $5116 / $5117)
//
//   $5113 selects the 8 KB bank mapped at $6000-$7FFF.
//     bit 7 is ignored (always PRG-RAM); bits 2-0 are the 8 KB bank index.
//
//   $5114-$5117 select the PRG windows starting at $8000.
//     bit 7 = 0 → PRG-RAM bank;  bit 7 = 1 → PRG-ROM bank.
//     $5117 always selects a PRG-ROM bank (bit 7 forced 1 by hardware).
//
// ─── CHR banking ──────────────────────────────────────────────────────────────
//   $5101 CHR mode (bits 1-0):
//     0 = 8 KB window ($0000-$1FFF selected by $5127 / $512B depending on fetch type)
//     1 = two 4 KB windows
//     2 = four 2 KB windows
//     3 = eight 1 KB windows (most flexible)
//
//   In 8×8 sprite mode (ppuctrl bit 5 = 0):
//     All tile fetches (BG and sprite) use CHR bank-set A ($5120-$5127).
//   In 8×16 sprite mode (ppuctrl bit 5 = 1):
//     Sprite tile fetches use bank-set A ($5120-$5127).
//     Background tile fetches use bank-set B ($5128-$512B).
//
//   The PPU signals which type of fetch is active by setting
//   map->ppu_sprite_fetch before calling the ppu_read hook.
//
// ─── Nametable mapping ────────────────────────────────────────────────────────
//   $5105 maps each of the four nametable slots independently:
//     bits 1-0 = NT A ($2000-$23FF)
//     bits 3-2 = NT B ($2400-$27FF)
//     bits 5-4 = NT C ($2800-$2BFF)
//     bits 7-6 = NT D ($2C00-$2FFF)
//   Each 2-bit field:
//     0 = CIRAM bank 0 (ppu.nametable[0x000-0x3FF])
//     1 = CIRAM bank 1 (ppu.nametable[0x400-0x7FF])
//     2 = ExRAM ($5C00-$5FFF, mode 0 or 1 only; otherwise reads 0)
//     3 = fill-mode (tile from $5106, attr from $5107)
//
// ─── ExRAM ($5C00-$5FFF) ──────────────────────────────────────────────────────
//   $5104 ExRAM mode:
//     0 = use as extra nametable (writable during rendering, read-only otherwise)
//     1 = use as nametable + attribute extension (same access rules)
//     2 = general-purpose R/W RAM (always writable)
//     3 = read-only RAM (writes ignored)
//
// ─── Scanline IRQ ─────────────────────────────────────────────────────────────
//   $5203 = IRQ scanline latch
//   $5204 (write) bit 7 = IRQ enable
//   $5204 (read)  bit 7 = in-frame flag, bit 6 = IRQ pending
//   IRQ fires when the scanline counter equals the latch and IRQ is enabled.
//   The PPU calls the scanline hook (map->scanline) at dot 260 of each scanline.
//
// ─── Multiply unit ────────────────────────────────────────────────────────────
//   Write operands to $5205 (A) and $5206 (B); read $5205/$5206 to get the
//   16-bit unsigned product (low byte / high byte).
//
// ─── Audio ────────────────────────────────────────────────────────────────────
//   MMC5 pulse channels ($5000-$5007) and PCM ($5011) are stubbed to silence.

#include "mapper_005.h"
#include <stdlib.h>
#include <string.h>

// Maximum PRG-RAM: 8 banks × 8 KB = 64 KB
#define MMC5_PRG_RAM_BANKS 8
#define MMC5_PRG_RAM_SIZE  (MMC5_PRG_RAM_BANKS * 0x2000)  // 64 KB

struct mmc5_ctx {
    // ── Control registers ──────────────────────────────────────────────────
    uint8_t prg_mode;       // $5100 bits 1-0  (0-3)
    uint8_t chr_mode;       // $5101 bits 1-0  (0-3)
    uint8_t wram_protect1;  // $5102 bits 1-0  (must be 0b10 together with protect2)
    uint8_t wram_protect2;  // $5103 bits 1-0  (must be 0b01 together with protect1)
    uint8_t exram_mode;     // $5104 bits 1-0  (0=NT, 1=NT+attr, 2=R/W, 3=R/O)
    uint8_t nt_mapping;     // $5105 — 2 bits per nametable, 4 nametables
    uint8_t fill_tile;      // $5106
    uint8_t fill_attr;      // $5107 bits 1-0

    // ── PRG bank registers ─────────────────────────────────────────────────
    // prg_bank[0] = $5113  ($6000-$7FFF, RAM only)
    // prg_bank[1] = $5114  ($8000 window in mode 3)
    // prg_bank[2] = $5115
    // prg_bank[3] = $5116
    // prg_bank[4] = $5117  (always ROM, bit 7 forced)
    uint8_t prg_bank[5];

    // ── CHR bank registers ─────────────────────────────────────────────────
    // Bank-set A ($5120-$5127): 8 × 1 KB registers
    uint8_t chr_bankA[8];
    // Bank-set B ($5128-$512B): 4 × 1 KB registers
    uint8_t chr_bankB[4];
    // Which bank-set was written last — used to determine the "last written"
    // register in modes 0/1/2 where only one register controls the window.
    uint8_t last_chr_set;   // 0 = A, 1 = B

    // ── Scanline IRQ ──────────────────────────────────────────────────────
    uint8_t irq_latch;      // $5203 — target scanline
    uint8_t irq_enable;     // $5204 bit 7
    uint8_t irq_pending;    // $5204 bit 6 (readable status)
    uint8_t in_frame;       // set when PPU is rendering visible scanlines
    uint8_t scanline_count; // scanlines seen since last VBlank

    // ── Multiply unit ─────────────────────────────────────────────────────
    uint8_t mult_a;         // $5205
    uint8_t mult_b;         // $5206

    // ── ExRAM (1 KB at $5C00-$5FFF) ──────────────────────────────────────
    uint8_t exram[0x400];

    // ── PRG-RAM (64 KB = 8 × 8 KB banks) ─────────────────────────────────
    uint8_t prg_ram[MMC5_PRG_RAM_SIZE];
};

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void mapper_005_init(struct mapper *map) {
    struct mmc5_ctx *ctx = calloc(1, sizeof(struct mmc5_ctx));
    if (!ctx) return;

    // Power-on defaults
    ctx->prg_mode  = 3;       // 8 KB × 4 windows
    ctx->chr_mode  = 3;       // 1 KB × 8 windows
    ctx->nt_mapping = 0xFF;   // all nametables → fill-mode (safe default)
    // $5117 always maps to the last PRG-ROM bank; bit 7 = ROM
    uint8_t last_rom = (map->num_prg_rom > 0)
                       ? (uint8_t)(((uint16_t)map->num_prg_rom * 2 - 1) | 0x80)
                       : 0xFF;
    ctx->prg_bank[4] = last_rom;  // $5117: last 8 KB ROM bank
    ctx->prg_bank[3] = last_rom;  // $5116 default (mode 2: last ROM)
    ctx->prg_bank[2] = last_rom;  // $5115 default
    ctx->prg_bank[0] = 0x00;      // $5113: PRG-RAM bank 0

    map->ctx          = ctx;
    map->ctx_size     = sizeof(struct mmc5_ctx);
    map->prg_ram      = ctx->prg_ram;
    map->prg_ram_size = MMC5_PRG_RAM_SIZE;
}

// ---------------------------------------------------------------------------
// PRG banking helpers
// ---------------------------------------------------------------------------

// Returns true when the PRG-RAM write-protect combination is unlocked.
// Hardware requires $5102 bits 1-0 == 0b10 AND $5103 bits 1-0 == 0b01.
static int prg_ram_writable(struct mmc5_ctx *ctx) {
    return (ctx->wram_protect1 & 0x03) == 0x02 &&
           (ctx->wram_protect2 & 0x03) == 0x01;
}

// Resolve a PRG address in $8000-$FFFF to an offset into prg_rom[].
// Returns 0 on error.  Sets *is_ram if the bank register selects PRG-RAM,
// and *ram_offset to the byte offset within prg_ram[].
static uint32_t resolve_prg(struct mmc5_ctx *ctx, uint16_t addr,
                             uint16_t num_8kb,
                             int *is_ram, uint32_t *ram_offset) {
    *is_ram     = 0;
    *ram_offset = 0;

    uint8_t bank_reg;
    uint16_t win_off;   // byte offset within the 8 KB / 16 KB / 32 KB window

    switch (ctx->prg_mode) {
    // ── Mode 0: single 32 KB window ($8000-$FFFF via $5117) ───────────────
    case 0: {
        uint8_t raw = ctx->prg_bank[4];
        uint16_t b32 = (raw & 0x7C) >> 2;   // bits 6-2 form the 32 KB bank number
        win_off = addr & 0x7FFF;
        // $5117 always selects ROM
        uint32_t bank_base = (uint32_t)b32 * 0x8000;
        if (num_8kb == 0) return 0;
        return (bank_base + win_off) % ((uint32_t)num_8kb * 0x2000);
    }

    // ── Mode 1: two 16 KB windows ($8000 via $5115, $C000 via $5117) ──────
    case 1:
        if (addr < 0xC000) {
            bank_reg = ctx->prg_bank[2];   // $5115
            win_off  = addr & 0x3FFF;
        } else {
            bank_reg = ctx->prg_bank[4];   // $5117 (always ROM)
            win_off  = addr & 0x3FFF;
        }
        // 16 KB banks: bit 0 of the bank register is ignored (16 KB aligned)
        if (bank_reg & 0x80) {  // ROM
            uint16_t b16 = (bank_reg & 0x7E) >> 1;
            uint32_t bank_base = (uint32_t)b16 * 0x4000;
            if (num_8kb == 0) return 0;
            return (bank_base + win_off) % ((uint32_t)num_8kb * 0x2000);
        } else {  // RAM
            *is_ram = 1;
            uint16_t b16 = (bank_reg & 0x0E) >> 1;
            *ram_offset = ((uint32_t)b16 * 0x4000 + win_off) % MMC5_PRG_RAM_SIZE;
            return 0;
        }

    // ── Mode 2: 16 KB + 8 KB + 8 KB ($5115 / $5116 / $5117) ─────────────
    case 2:
        if (addr < 0xC000) {
            bank_reg = ctx->prg_bank[2];   // $5115 (16 KB window)
            win_off  = addr & 0x3FFF;
            if (bank_reg & 0x80) {
                uint16_t b16 = (bank_reg & 0x7E) >> 1;
                uint32_t bank_base = (uint32_t)b16 * 0x4000;
                if (num_8kb == 0) return 0;
                return (bank_base + win_off) % ((uint32_t)num_8kb * 0x2000);
            } else {
                *is_ram = 1;
                uint16_t b16 = (bank_reg & 0x0E) >> 1;
                *ram_offset = ((uint32_t)b16 * 0x4000 + win_off) % MMC5_PRG_RAM_SIZE;
                return 0;
            }
        } else if (addr < 0xE000) {
            bank_reg = ctx->prg_bank[3];   // $5116
            win_off  = addr & 0x1FFF;
        } else {
            bank_reg = ctx->prg_bank[4];   // $5117 (always ROM)
            win_off  = addr & 0x1FFF;
        }
        goto resolve_8kb;

    // ── Mode 3: four 8 KB windows ($5114-$5117) ───────────────────────────
    default:  // case 3
        if (addr < 0xA000) {
            bank_reg = ctx->prg_bank[1];   // $5114
        } else if (addr < 0xC000) {
            bank_reg = ctx->prg_bank[2];   // $5115
        } else if (addr < 0xE000) {
            bank_reg = ctx->prg_bank[3];   // $5116
        } else {
            bank_reg = ctx->prg_bank[4];   // $5117 (always ROM)
        }
        win_off = addr & 0x1FFF;
        goto resolve_8kb;
    }

resolve_8kb:
    if (bank_reg & 0x80) {  // ROM
        uint16_t b8 = bank_reg & 0x7F;
        if (num_8kb == 0) return 0;
        uint32_t bank_base = ((uint32_t)b8 % num_8kb) * 0x2000;
        return bank_base + win_off;
    } else {  // RAM
        *is_ram = 1;
        uint8_t b8 = bank_reg & 0x07;
        *ram_offset = ((uint32_t)b8 * 0x2000 + win_off) % MMC5_PRG_RAM_SIZE;
        return 0;
    }
}

// ---------------------------------------------------------------------------
// CHR banking helper
// ---------------------------------------------------------------------------
//
// The MMC5 has two CHR bank-sets:
//   Set A ($5120-$5127): used for sprite tile fetches; also used for ALL
//           fetches when 8×8 sprite mode is active (the common case).
//   Set B ($5128-$512B): used for background tile fetches in 8×16 mode.
//
// The PPU sets map->ppu_sprite_fetch = 1 before calling ppu_read for sprite
// tiles.  We use this to select the correct bank-set.
//
// In CHR modes 0-2 the hardware uses only the *last written* register of each
// set; here we always read registers [3] (set A: $5127) / [3] (set B: $512B)
// for the "last" register, which is correct when banks are configured
// sequentially from $5120 upward.

static uint8_t resolve_chr(struct mmc5_ctx *ctx, struct mapper *map,
                            uint16_t addr) {
    // Determine which bank-set to use.
    // ppu_sprite_fetch = 1 → sprite fetch → use set A.
    // ppu_sprite_fetch = 0 → BG fetch → use set B (8×16) or set A (8×8).
    // We decide based on last_chr_set: if bank B was written, use B for BG.
    int use_A = map->ppu_sprite_fetch || (ctx->last_chr_set == 0);

    uint16_t num_1kb = (uint16_t)map->num_chr_rom * 8;
    if (num_1kb == 0) return 0;  // CHR-RAM handled in ppu_read

    uint16_t bank;

    switch (ctx->chr_mode) {
    // ── Mode 0: 8 KB window ──────────────────────────────────────────────
    case 0: {
        // The last register in each set controls the entire 8 KB.
        // Set A: $5127 (index 7); Set B: $512B (index 3).
        uint8_t raw = use_A ? ctx->chr_bankA[7] : ctx->chr_bankB[3];
        // Bits 7-0 form the 1 KB bank; the 8 KB window uses the low 3 bits
        // of the address for fine offset, upper bits select which 8 KB block.
        uint16_t b8  = (raw & 0xFF) >> 3;          // 8 KB bank index
        uint16_t off = (uint16_t)b8 * 8 + ((addr >> 10) & 0x07); // 1 KB sub-bank
        bank = off % num_1kb;
        break;
    }
    // ── Mode 1: 4 KB × 2 ─────────────────────────────────────────────────
    case 1: {
        // Set A: $5123 (lo), $5127 (hi); Set B: $5129 (lo idx 1), $512B (hi idx 3)
        int hi = (addr >= 0x1000);
        uint8_t raw = use_A ? (hi ? ctx->chr_bankA[7] : ctx->chr_bankA[3])
                            : (hi ? ctx->chr_bankB[3] : ctx->chr_bankB[1]);
        uint16_t b4  = (raw & 0xFF) >> 2;
        uint16_t off = (uint16_t)b4 * 4 + ((addr >> 10) & 0x03);
        bank = off % num_1kb;
        break;
    }
    // ── Mode 2: 2 KB × 4 ─────────────────────────────────────────────────
    case 2: {
        uint8_t slot = (addr >> 11) & 0x03;  // 2 KB slot 0-3
        uint8_t raw;
        if (use_A) {
            // Slots 0-3 → $5121, $5123, $5125, $5127 (indices 1,3,5,7)
            raw = ctx->chr_bankA[slot * 2 + 1];
        } else {
            // Slots 0-1 → $5129, $512B (indices 1,3)
            raw = ctx->chr_bankB[(slot & 0x01) * 2 + 1];
        }
        uint16_t b2  = (raw & 0xFF) >> 1;
        uint16_t off = (uint16_t)b2 * 2 + ((addr >> 10) & 0x01);
        bank = off % num_1kb;
        break;
    }
    // ── Mode 3: 1 KB × 8 (default) ───────────────────────────────────────
    default: {
        uint8_t slot = (addr >> 10) & 0x07;  // 1 KB slot 0-7
        uint8_t raw;
        if (use_A) {
            raw = ctx->chr_bankA[slot];                 // $5120-$5127
        } else {
            raw = ctx->chr_bankB[slot & 0x03];          // $5128-$512B (mirror for slots 4-7)
        }
        bank = (uint16_t)raw % num_1kb;
        break;
    }
    }

    uint32_t offset = (uint32_t)bank * 0x0400 + (addr & 0x03FF);
    if (offset < map->cartridge->chr_rom_len)
        return map->cartridge->chr_rom[offset];
    return 0;
}

// ---------------------------------------------------------------------------
// Nametable hooks (called by PPU instead of default CIRAM path)
// ---------------------------------------------------------------------------

uint8_t mapper_005_nt_read(struct mapper *map, uint16_t addr) {
    struct mmc5_ctx *ctx = map->ctx;

    // Normalise to $2000-$2FFF (the four physical nametable pages)
    addr = 0x2000 + ((addr - 0x2000) & 0x0FFF);
    uint8_t nt    = (addr >> 10) & 0x03;   // which nametable (0-3)
    uint16_t off  = addr & 0x03FF;         // byte offset within 1 KB page

    uint8_t slot = (ctx->nt_mapping >> (nt * 2)) & 0x03;

    switch (slot) {
    case 0:  // CIRAM bank 0
        return map->ciram ? map->ciram[off] : 0;
    case 1:  // CIRAM bank 1
        return map->ciram ? map->ciram[0x0400 + off] : 0;
    case 2:  // ExRAM (readable only in modes 0 and 1)
        if (ctx->exram_mode <= 1)
            return ctx->exram[off & 0x3FF];
        return 0;
    case 3:  // Fill-mode
        if (off < 0x3C0)
            return ctx->fill_tile;
        // Attribute area: fill_attr fills all 2-bit fields of the byte
        return (uint8_t)(ctx->fill_attr |
                         (ctx->fill_attr << 2) |
                         (ctx->fill_attr << 4) |
                         (ctx->fill_attr << 6));
    }
    return 0;
}

void mapper_005_nt_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct mmc5_ctx *ctx = map->ctx;

    addr = 0x2000 + ((addr - 0x2000) & 0x0FFF);
    uint8_t nt   = (addr >> 10) & 0x03;
    uint16_t off = addr & 0x03FF;

    uint8_t slot = (ctx->nt_mapping >> (nt * 2)) & 0x03;

    switch (slot) {
    case 0:
        if (map->ciram) map->ciram[off] = data;
        break;
    case 1:
        if (map->ciram) map->ciram[0x0400 + off] = data;
        break;
    case 2:
        // ExRAM: writable in modes 0 and 1 only during PPU rendering
        // (we allow writes unconditionally since we have no rendering context).
        if (ctx->exram_mode <= 1)
            ctx->exram[off & 0x3FF] = data;
        else if (ctx->exram_mode == 2)
            ctx->exram[off & 0x3FF] = data;
        break;
    case 3:  // fill-mode — writes ignored
        break;
    }
}

// ---------------------------------------------------------------------------
// Scanline IRQ hook — called by PPU at dot 260 of each scanline
// ---------------------------------------------------------------------------

void mapper_005_scanline(struct mapper *map) {
    struct mmc5_ctx *ctx = map->ctx;

    if (!ctx->in_frame) {
        ctx->in_frame      = 1;
        ctx->scanline_count = 0;
    } else {
        ctx->scanline_count++;
    }

    // IRQ fires when the scanline counter matches the latch.
    if (ctx->scanline_count == ctx->irq_latch && ctx->irq_enable) {
        ctx->irq_pending = 1;
        map->irq_pending = 1;
    }

    // After scanline 239 the PPU enters VBlank; reset in_frame.
    if (ctx->scanline_count >= 239) {
        ctx->in_frame       = 0;
        ctx->scanline_count = 0;
    }
}

// ---------------------------------------------------------------------------
// CPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_005_cpu_read(struct mapper *map, uint16_t addr) {
    struct mmc5_ctx *ctx = map->ctx;

    // ── $5C00-$5FFF ExRAM ─────────────────────────────────────────────────
    if (addr >= 0x5C00 && addr <= 0x5FFF) {
        // Readable in modes 2 and 3
        if (ctx->exram_mode >= 2)
            return ctx->exram[addr & 0x3FF];
        return 0;
    }

    // ── $5000-$5BFF: MMC5 internal registers (read-back / status) ─────────
    if (addr >= 0x5000 && addr <= 0x5BFF) {
        switch (addr) {
        // Audio status ($5015) — stubs
        case 0x5015:
            return 0x00;

        // IRQ status ($5204)
        case 0x5204: {
            uint8_t v = (ctx->in_frame    ? 0x40 : 0x00) |
                        (ctx->irq_pending  ? 0x80 : 0x00);
            ctx->irq_pending = 0;
            map->irq_pending = 0;
            return v;
        }

        // Multiply result
        case 0x5205: {
            uint16_t product = (uint16_t)ctx->mult_a * (uint16_t)ctx->mult_b;
            return (uint8_t)(product & 0xFF);
        }
        case 0x5206: {
            uint16_t product = (uint16_t)ctx->mult_a * (uint16_t)ctx->mult_b;
            return (uint8_t)(product >> 8);
        }

        default:
            return 0xFF; // open bus
        }
    }

    // ── $6000-$7FFF PRG-RAM ($5113) ──────────────────────────────────────
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        uint8_t b = ctx->prg_bank[0] & 0x07;
        uint32_t off = (uint32_t)b * 0x2000 + (addr & 0x1FFF);
        return ctx->prg_ram[off % MMC5_PRG_RAM_SIZE];
    }

    // ── $8000-$FFFF PRG-ROM / PRG-RAM ────────────────────────────────────
    if (addr >= 0x8000) {
        uint16_t num_8kb = (uint16_t)map->num_prg_rom * 2;
        int is_ram = 0;
        uint32_t ram_off = 0;
        uint32_t rom_off = resolve_prg(ctx, addr, num_8kb, &is_ram, &ram_off);
        if (is_ram)
            return ctx->prg_ram[ram_off % MMC5_PRG_RAM_SIZE];
        if (rom_off < map->cartridge->prg_rom_len)
            return map->cartridge->prg_rom[rom_off];
        return 0;
    }

    return 0;
}

void mapper_005_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct mmc5_ctx *ctx = map->ctx;

    // ── $5000-$5007 MMC5 pulse channels (audio — stubbed) ─────────────────
    // ── $5010-$5011 MMC5 PCM (audio — stubbed) ────────────────────────────
    // ── $5015 APU status (ignore writes) ──────────────────────────────────

    // ── $5100-$5107 Control registers ─────────────────────────────────────
    if (addr >= 0x5100 && addr <= 0x5107) {
        switch (addr) {
        case 0x5100: ctx->prg_mode      = data & 0x03; break;
        case 0x5101: ctx->chr_mode      = data & 0x03; break;
        case 0x5102: ctx->wram_protect1 = data & 0x03; break;
        case 0x5103: ctx->wram_protect2 = data & 0x03; break;
        case 0x5104: ctx->exram_mode    = data & 0x03; break;
        case 0x5105: ctx->nt_mapping    = data;        break;
        case 0x5106: ctx->fill_tile     = data;        break;
        case 0x5107: ctx->fill_attr     = data & 0x03; break;
        }
        return;
    }

    // ── $5113-$5117 PRG bank registers ────────────────────────────────────
    if (addr >= 0x5113 && addr <= 0x5117) {
        uint8_t idx = (uint8_t)(addr - 0x5113);
        if (idx == 4) data |= 0x80;  // $5117 always ROM
        ctx->prg_bank[idx] = data;
        return;
    }

    // ── $5120-$5127 CHR bank-set A ────────────────────────────────────────
    if (addr >= 0x5120 && addr <= 0x5127) {
        ctx->chr_bankA[addr - 0x5120] = data;
        ctx->last_chr_set = 0;
        return;
    }

    // ── $5128-$512B CHR bank-set B ────────────────────────────────────────
    if (addr >= 0x5128 && addr <= 0x512B) {
        ctx->chr_bankB[addr - 0x5128] = data;
        ctx->last_chr_set = 1;
        return;
    }

    // ── $5203 Scanline IRQ latch ──────────────────────────────────────────
    if (addr == 0x5203) {
        ctx->irq_latch = data;
        return;
    }

    // ── $5204 Scanline IRQ control ────────────────────────────────────────
    if (addr == 0x5204) {
        ctx->irq_enable  = (data >> 7) & 0x01;
        ctx->irq_pending = 0;
        map->irq_pending = 0;
        return;
    }

    // ── $5205-$5206 Multiply operands ─────────────────────────────────────
    if (addr == 0x5205) { ctx->mult_a = data; return; }
    if (addr == 0x5206) { ctx->mult_b = data; return; }

    // ── $5C00-$5FFF ExRAM ─────────────────────────────────────────────────
    if (addr >= 0x5C00 && addr <= 0x5FFF) {
        if (ctx->exram_mode <= 2)  // modes 0, 1, 2 allow writes; mode 3 = R/O
            ctx->exram[addr & 0x3FF] = data;
        return;
    }

    // ── $6000-$7FFF PRG-RAM ───────────────────────────────────────────────
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (prg_ram_writable(ctx)) {
            uint8_t b   = ctx->prg_bank[0] & 0x07;
            uint32_t off = (uint32_t)b * 0x2000 + (addr & 0x1FFF);
            ctx->prg_ram[off % MMC5_PRG_RAM_SIZE] = data;
        }
        return;
    }

    // ── $8000-$FFFF PRG-RAM windows (mode 2/3 with RAM bank registers) ────
    if (addr >= 0x8000) {
        if (!prg_ram_writable(ctx)) return;
        uint16_t num_8kb = (uint16_t)map->num_prg_rom * 2;
        int is_ram = 0;
        uint32_t ram_off = 0;
        resolve_prg(ctx, addr, num_8kb, &is_ram, &ram_off);
        if (is_ram)
            ctx->prg_ram[ram_off % MMC5_PRG_RAM_SIZE] = data;
    }
}

// ---------------------------------------------------------------------------
// PPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_005_ppu_read(struct mapper *map, uint16_t addr) {
    if (addr > 0x1FFF) return 0;  // nametable reads handled by nt_read hook

    struct mmc5_ctx *ctx = map->ctx;

    // CHR-RAM (when no CHR-ROM present)
    if (map->num_chr_rom == 0 || map->cartridge->chr_ram_allocated) {
        if (map->cartridge->chr_rom)
            return map->cartridge->chr_rom[addr & 0x1FFF];
        return 0;
    }

    return resolve_chr(ctx, map, addr);
}

void mapper_005_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (addr > 0x1FFF) return;
    if (!map->cartridge->chr_ram_allocated) return;
    if (map->cartridge->chr_rom)
        map->cartridge->chr_rom[addr & 0x1FFF] = data;
}
