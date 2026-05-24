// Mapper 019 — Namco 163 (N163)
//
// PRG-ROM: up to 512 KB in four 8 KB windows.
//   Banks 0-2 ($8000/$A000/$C000) are software-switchable via registers
//   $E000/$E800/$F000.  Bank 3 ($E000-$FFFF) is always fixed to the last
//   8 KB bank.
//
// CHR-ROM: up to 1 MB banked in eight 1 KB windows ($0000-$1FFF), each
//   independently selectable via registers $8000-$BFFF (one register per
//   1 KB window, spaced 0x800 bytes apart).
//
// Nametable CHR extension ($C000-$DFFF): each nametable slot can be driven
//   from CHR-ROM (bit 6 = 1) or from internal CIRAM.  This implementation
//   uses normal H/V/single mirroring for nametable reads; CHR-extension is
//   not implemented (games that need it are rare and require PPU-level hooks).
//
// Mirroring ($F800 bits 1-0): 00=external (treated as single-lo), 01=H,
//   10=V, 11=single-lo.
//
// IRQ: 16-bit counter that decrements every CPU cycle when enabled.  When it
//   wraps through 0x7FFF→0 (bit 15 is the enable), irq_pending is set.
//   The counter is stored as a 15-bit value (bits 14-0); bit 15 of the high
//   byte is the enable flag.
//   - $5000 write: low 8 bits of counter
//   - $5800 write: high 7 bits (bits 14-8) + bit 15 = enable flag
//   - $5000 read:  low 8 bits
//   - $5800 read:  high 8 bits (including enable flag in bit 7)
//
// Internal RAM: 128 bytes accessed via $4800-$4FFF (addr & 0x7F).
//
// Audio: Namco 163 has 8 wavetable channels — stubbed to silence.

#include "mapper_019.h"
#include <stdlib.h>
#include <string.h>

struct n163_ctx {
    uint8_t  prg_bank[3];       // 8KB PRG banks at $8000/$A000/$C000
    uint8_t  chr_bank[8];       // 1KB CHR bank indices (PPU $0000-$1FFF)
    uint8_t  nt_bank[4];        // nametable CHR bank values ($C000-$DFFF regs)
    uint8_t  mirroring;
    uint8_t  internal_ram[128]; // Namco 163 internal RAM
    uint16_t irq_counter;       // 15-bit counter (bits 14-0)
    uint8_t  irq_enable;        // bit 15 of $5800
    uint8_t  irq_pending;       // set when counter crosses 0x7FFF→wrap
    uint8_t  chr_ram_lo;        // $E800 bit 6: CHR-RAM at $0000
    uint8_t  chr_ram_hi;        // $E800 bit 7: CHR-RAM at $1000
    uint8_t  chr_ram[0x2000];   // 8KB CHR-RAM
};

void mapper_019_init(struct mapper *map) {
    struct n163_ctx *ctx = calloc(1, sizeof(struct n163_ctx));
    if (!ctx) return;
    map->ctx      = ctx;
    map->ctx_size = sizeof(struct n163_ctx);
    // mirroring seeded from ROM header by mapper.c
}

// ---------------------------------------------------------------------------
// IRQ — called once per CPU cycle (wired to mapper->clock)
// ---------------------------------------------------------------------------
// The Namco 163 IRQ counter counts up every CPU cycle when enabled.  An IRQ
// fires when the 15-bit counter transitions from $7FFF to $8000 (i.e., when
// bit 15 would be set — the hardware uses bit 15 as the "overflow" trigger).
// After firing the counter keeps running; the IRQ line stays asserted until
// the CPU acknowledges.

void mapper_019_cpu_cycle(struct mapper *map) {
    struct n163_ctx *ctx = map->ctx;
    if (!ctx->irq_enable) return;

    if (ctx->irq_counter < 0x7FFF) {
        ctx->irq_counter++;
    } else {
        // Counter has saturated at $7FFF; wraps to 0 on next tick — fire IRQ
        ctx->irq_counter = 0;
        ctx->irq_pending = 1;
        map->irq_pending = 1;
    }
}

// ---------------------------------------------------------------------------
// CHR bank selection helper
// ---------------------------------------------------------------------------

// Returns the byte for the given PPU address in the pattern table area
// ($0000-$1FFF).  Handles CHR-ROM banking and optional CHR-RAM windows.
static uint8_t ppu_chr_read(struct n163_ctx *ctx, struct mapper *map,
                             uint16_t addr) {
    uint8_t slot  = (addr >> 10) & 0x07; // which 1KB window (0-7)
    uint8_t lo    = (slot < 4);           // lower 4KB half?

    // Check if this half is CHR-RAM
    if (lo && ctx->chr_ram_lo)
        return ctx->chr_ram[addr & 0x1FFF];
    if (!lo && ctx->chr_ram_hi)
        return ctx->chr_ram[addr & 0x1FFF];

    // CHR-ROM — use the appropriate 1KB bank register
    uint32_t num_1kb = (uint32_t)map->num_chr_rom * 8;
    if (num_1kb == 0) return 0;

    uint32_t bank   = ctx->chr_bank[slot] % num_1kb;
    uint32_t offset = bank * 0x0400 + (addr & 0x03FF);
    if (offset < map->cartridge->chr_rom_len)
        return map->cartridge->chr_rom[offset];
    return 0;
}

static void ppu_chr_write(struct n163_ctx *ctx, uint16_t addr, uint8_t data) {
    uint8_t slot = (addr >> 10) & 0x07;
    uint8_t lo   = (slot < 4);
    if (lo && ctx->chr_ram_lo)
        ctx->chr_ram[addr & 0x1FFF] = data;
    if (!lo && ctx->chr_ram_hi)
        ctx->chr_ram[addr & 0x1FFF] = data;
}

// ---------------------------------------------------------------------------
// CPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_019_cpu_read(struct mapper *map, uint16_t addr) {
    struct n163_ctx *ctx = map->ctx;

    // Internal RAM at $4800-$4FFF
    if (addr >= 0x4800 && addr <= 0x4FFF)
        return ctx->internal_ram[addr & 0x7F];

    // IRQ counter low byte
    if (addr >= 0x5000 && addr <= 0x57FF)
        return (uint8_t)(ctx->irq_counter & 0xFF);

    // IRQ counter high byte + enable flag
    if (addr >= 0x5800 && addr <= 0x5FFF)
        return (uint8_t)(((ctx->irq_counter >> 8) & 0x7F) |
                         (ctx->irq_enable ? 0x80 : 0x00));

    // PRG-ROM area
    if (addr < 0x8000) return 0;

    uint32_t num_8kb = (uint32_t)map->num_prg_rom * 2;
    if (num_8kb == 0) return 0;

    uint32_t bank;
    if (addr <= 0x9FFF)
        bank = ctx->prg_bank[0] % num_8kb;
    else if (addr <= 0xBFFF)
        bank = ctx->prg_bank[1] % num_8kb;
    else if (addr <= 0xDFFF)
        bank = ctx->prg_bank[2] % num_8kb;
    else
        bank = num_8kb - 1; // fixed last bank

    uint32_t offset = bank * 0x2000 + (addr & 0x1FFF);
    if (offset < map->cartridge->prg_rom_len)
        return map->cartridge->prg_rom[offset];
    return 0;
}

void mapper_019_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct n163_ctx *ctx = map->ctx;

    // Internal RAM at $4800-$4FFF
    if (addr >= 0x4800 && addr <= 0x4FFF) {
        ctx->internal_ram[addr & 0x7F] = data;
        return;
    }

    // IRQ counter low 8 bits
    if (addr >= 0x5000 && addr <= 0x57FF) {
        ctx->irq_counter = (ctx->irq_counter & 0x7F00) | data;
        // Acknowledge any pending IRQ on counter write
        ctx->irq_pending = 0;
        map->irq_pending = 0;
        return;
    }

    // IRQ counter high 7 bits + enable flag
    if (addr >= 0x5800 && addr <= 0x5FFF) {
        ctx->irq_counter = (ctx->irq_counter & 0x00FF) |
                           ((uint16_t)(data & 0x7F) << 8);
        ctx->irq_enable  = (data >> 7) & 0x01;
        ctx->irq_pending = 0;
        map->irq_pending = 0;
        return;
    }

    if (addr < 0x8000) return;

    // CHR bank registers ($8000-$BFFF) — one per 1KB window
    // $8000-$87FF → chr_bank[0] (PPU $0000-$03FF)
    // $8800-$8FFF → chr_bank[1] (PPU $0400-$07FF)
    // ...
    // $B800-$BFFF → chr_bank[7] (PPU $1C00-$1FFF)
    if (addr <= 0xBFFF) {
        uint8_t slot = (uint8_t)((addr - 0x8000) >> 11);
        ctx->chr_bank[slot] = data;
        return;
    }

    // Nametable CHR/NT bank registers ($C000-$DFFF)
    // $C000-$C7FF → nt_bank[0] ($2000)
    // $C800-$CFFF → nt_bank[1] ($2400)
    // $D000-$D7FF → nt_bank[2] ($2800)
    // $D800-$DFFF → nt_bank[3] ($2C00)
    if (addr <= 0xDFFF) {
        uint8_t slot = (uint8_t)((addr - 0xC000) >> 11);
        ctx->nt_bank[slot] = data;
        return;
    }

    // PRG bank registers
    // $E000-$E7FF → prg_bank[0] (bits 5-0 = bank; bit 6 ignored = audio disable)
    if (addr <= 0xE7FF) {
        ctx->prg_bank[0] = data & 0x3F;
        return;
    }

    // $E800-$EFFF → prg_bank[1]; bit 6 = CHR-RAM lo enable; bit 7 = CHR-RAM hi enable
    if (addr <= 0xEFFF) {
        ctx->prg_bank[1]  = data & 0x3F;
        ctx->chr_ram_lo   = (data >> 6) & 0x01;
        ctx->chr_ram_hi   = (data >> 7) & 0x01;
        return;
    }

    // $F000-$F7FF → prg_bank[2]
    if (addr <= 0xF7FF) {
        ctx->prg_bank[2] = data & 0x3F;
        return;
    }

    // $F800-$FFFF → mirroring (bits 1-0); audio bits ignored
    // 00 = external/ignore (treat as single-lo)
    // 01 = horizontal
    // 10 = vertical
    // 11 = single-lo
    {
        uint8_t m = data & 0x03;
        switch (m) {
        case 0: map->mirroring = MIRROR_SINGLE_LO; break;
        case 1: map->mirroring = MIRROR_HORIZONTAL; break;
        case 2: map->mirroring = MIRROR_VERTICAL;   break;
        case 3: map->mirroring = MIRROR_SINGLE_LO;  break;
        }
        ctx->mirroring = map->mirroring;
    }
}

// ---------------------------------------------------------------------------
// PPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_019_ppu_read(struct mapper *map, uint16_t addr) {
    struct n163_ctx *ctx = map->ctx;

    if (addr <= 0x1FFF)
        return ppu_chr_read(ctx, map, addr);

    return 0; // nametable reads handled by PPU/bus directly
}

void mapper_019_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct n163_ctx *ctx = map->ctx;

    if (addr <= 0x1FFF)
        ppu_chr_write(ctx, addr, data);
}
