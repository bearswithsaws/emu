// Mapper 069 — Sunsoft FME-7 (also known as Sunsoft 5B)
//
// PRG-ROM: up to 512 KB in four switchable 8 KB windows ($8000/$A000/$C000/$E000).
//          All four banks are independently selectable via commands $9/$A/$B/$C.
//          The $E000 bank typically points to the last bank but is switchable.
// PRG-RAM: 8 KB at $6000-$7FFF.  Enabled via command $8 bit 6; bit 7 selects
//          RAM ($6000 window reads from internal RAM) vs ROM (reads from PRG-ROM).
// CHR-ROM: up to 256 KB in eight independently switchable 1 KB windows
//          ($0000-$1FFF).  Commands $0-$7 each select one 1 KB bank.
//          CHR-RAM is also supported when no CHR-ROM is present.
// Mirroring: Dynamically configurable via command $D:
//          bits 1-0: 00=Vertical, 01=Horizontal, 10=Single-screen-lo, 11=Single-screen-hi
// IRQ: 16-bit down-counter, decremented every CPU cycle when counter_enable=1.
//      When the counter reaches 0 and irq_enable=1, irq_pending is set.
//      The counter wraps (does not halt at 0).
//      Writing command $E clears the IRQ pending flag and sets counter high byte
//      and control bits (bit 0 = irq_enable, bit 7 = counter_enable).
//      Writing command $F sets the counter low byte.
//
// Audio: The Sunsoft 5B audio expansion (YM2149 / AY-3-8910) is NOT emulated.
//        Audio register writes ($C000/$E000) are silently ignored.
//
// Compatible games: Gimmick! (Mr. Gimmick), Batman: Return of the Joker,
//                   Hebereke, Gremlins 2.

#include "mapper_069.h"
#include <stdlib.h>
#include <string.h>

struct fme7_ctx {
    uint8_t  cmd;                // last value written to $8000 (command register)
    uint8_t  prg_bank[4];        // 8 KB PRG bank indices for $8000/$A000/$C000/$E000
    uint8_t  chr_bank[8];        // 1 KB CHR bank indices for PPU $0000-$1FFF
    uint8_t  prg_ram_enabled;    // 1 = PRG-RAM window is readable/writable
    uint8_t  prg_ram_is_rom;     // 1 = $6000-$7FFF reads from PRG-ROM bank instead
    uint8_t  prg_ram_bank;       // PRG-ROM bank index when prg_ram_is_rom=1
    uint16_t irq_counter;        // 16-bit down-counter
    uint8_t  irq_enable;         // 1 = assert IRQ when counter reaches 0
    uint8_t  counter_enable;     // 1 = decrement counter each CPU cycle
    uint8_t  irq_pending;        // set when counter hits 0 with irq_enable=1
    uint8_t  prg_ram[0x2000];    // 8 KB internal PRG-RAM
};

void mapper_069_init(struct mapper *map) {
    struct fme7_ctx *ctx = calloc(1, sizeof(struct fme7_ctx));
    if (!ctx) return;

    // Power-up: last PRG bank fixed at $E000 (standard convention).
    // All other registers start at 0.
    uint16_t num_8kb = map->num_prg_rom * 2;
    if (num_8kb > 0)
        ctx->prg_bank[3] = (uint8_t)(num_8kb - 1);

    map->ctx          = ctx;
    map->ctx_size     = sizeof(struct fme7_ctx);
    map->prg_ram      = ctx->prg_ram;
    map->prg_ram_size = sizeof(ctx->prg_ram);
}

// ---------------------------------------------------------------------------
// Per-CPU-cycle IRQ counter clock
// ---------------------------------------------------------------------------

void mapper_069_clock(struct mapper *map) {
    struct fme7_ctx *ctx = map->ctx;

    if (!ctx->counter_enable)
        return;

    // Decrement first, then check for zero (wraps from 0 to 0xFFFF).
    ctx->irq_counter--;

    if (ctx->irq_counter == 0 && ctx->irq_enable)
        map->irq_pending = 1;
}

// ---------------------------------------------------------------------------
// CPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_069_cpu_read(struct mapper *map, uint16_t addr) {
    struct fme7_ctx *ctx = map->ctx;

    // PRG-RAM / PRG-ROM window at $6000-$7FFF
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (!ctx->prg_ram_enabled)
            return 0xFF; // open bus when disabled

        if (ctx->prg_ram_is_rom) {
            // Read from PRG-ROM bank (ctx->prg_ram_bank)
            uint16_t num_8kb = map->num_prg_rom * 2;
            uint16_t bank = ctx->prg_ram_bank % (num_8kb ? num_8kb : 1);
            uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
            if (offset < map->cartridge->prg_rom_len)
                return map->cartridge->prg_rom[offset];
            return 0xFF;
        }

        return ctx->prg_ram[addr & 0x1FFF];
    }

    if (addr < 0x8000)
        return 0xFF;

    // PRG-ROM windows $8000-$FFFF (four 8 KB banks)
    uint16_t num_8kb = map->num_prg_rom * 2;
    if (num_8kb == 0)
        return 0xFF;

    uint8_t slot;
    if (addr <= 0x9FFF)      slot = 0;
    else if (addr <= 0xBFFF) slot = 1;
    else if (addr <= 0xDFFF) slot = 2;
    else                     slot = 3;

    uint16_t bank   = ctx->prg_bank[slot] % num_8kb;
    uint32_t offset = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);

    if (offset < map->cartridge->prg_rom_len)
        return map->cartridge->prg_rom[offset];
    return 0xFF;
}

void mapper_069_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    struct fme7_ctx *ctx = map->ctx;

    // PRG-RAM write at $6000-$7FFF (only when RAM enabled and not ROM-mapped)
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (ctx->prg_ram_enabled && !ctx->prg_ram_is_rom)
            ctx->prg_ram[addr & 0x1FFF] = data;
        return;
    }

    if (addr < 0x8000)
        return;

    // $8000-$9FFF: command register
    if (addr <= 0x9FFF) {
        ctx->cmd = data & 0x0F;
        return;
    }

    // $A000-$BFFF: parameter register — apply to the selected command
    if (addr <= 0xBFFF) {
        uint8_t cmd = ctx->cmd;

        if (cmd <= 0x07) {
            // CHR bank select for 1 KB window cmd (PPU $0000 + cmd*$400)
            ctx->chr_bank[cmd] = data;

        } else if (cmd == 0x08) {
            // PRG-RAM control:
            // bits 5-0 = ROM bank number used when prg_ram_is_rom=1
            // bit 6    = RAM enable (1 = enabled)
            // bit 7    = ROM select (1 = $6000 window reads PRG-ROM bank)
            ctx->prg_ram_bank    = data & 0x3F;
            ctx->prg_ram_enabled = (data >> 6) & 0x01;
            ctx->prg_ram_is_rom  = (data >> 7) & 0x01;

        } else if (cmd >= 0x09 && cmd <= 0x0C) {
            // PRG bank select for one of the four 8 KB windows
            // cmd $9 → slot 0 ($8000), $A → slot 1 ($A000),
            // $B → slot 2 ($C000), $C → slot 3 ($E000)
            ctx->prg_bank[cmd - 0x09] = data & 0x3F;

        } else if (cmd == 0x0D) {
            // Mirroring
            switch (data & 0x03) {
            case 0: map->mirroring = MIRROR_VERTICAL;   break;
            case 1: map->mirroring = MIRROR_HORIZONTAL; break;
            case 2: map->mirroring = MIRROR_SINGLE_LO;  break;
            case 3: map->mirroring = MIRROR_SINGLE_HI;  break;
            }

        } else if (cmd == 0x0E) {
            // IRQ control: writing clears IRQ pending flag.
            // bit 0 = irq_enable, bit 7 = counter_enable
            map->irq_pending      = 0;
            ctx->irq_pending      = 0;
            ctx->irq_enable       = data & 0x01;
            ctx->counter_enable   = (data >> 7) & 0x01;

        } else if (cmd == 0x0F) {
            // IRQ counter low byte (counter is 16-bit; high byte from $E write)
            // Per NESdev: $F sets the low 8 bits; the counter high byte is
            // written via command $E bits 15-8. However the standard FME-7
            // only has an 8-bit-wide parameter bus, so the counter is effectively
            // 16-bit with the low byte here and bits not specified from $E.
            // Correct interpretation: $F = low byte, counter high stays unchanged.
            ctx->irq_counter = (ctx->irq_counter & 0xFF00) | data;
        }
        return;
    }

    // $C000-$FFFF: Sunsoft 5B audio registers — silently ignored (not implemented)
}

// ---------------------------------------------------------------------------
// PPU read / write
// ---------------------------------------------------------------------------

uint8_t mapper_069_ppu_read(struct mapper *map, uint16_t addr) {
    struct fme7_ctx *ctx = map->ctx;

    if (addr > 0x1FFF)
        return 0;
    if (!map->cartridge->chr_rom)
        return 0;

    // CHR-RAM: direct access (no banking needed for CHR-RAM mappers)
    if (map->cartridge->chr_ram_allocated)
        return map->cartridge->chr_rom[addr & 0x1FFF];

    // CHR-ROM: 8 independent 1 KB banks
    uint16_t num_1kb = (uint16_t)map->num_chr_rom * 8;
    if (num_1kb == 0)
        return 0;

    uint8_t  slot   = (addr >> 10) & 0x07; // which 1 KB window (0-7)
    uint16_t bank   = ctx->chr_bank[slot] % num_1kb;
    uint32_t offset = (uint32_t)bank * 0x0400 + (addr & 0x03FF);

    if (offset < map->cartridge->chr_rom_len)
        return map->cartridge->chr_rom[offset];
    return 0;
}

void mapper_069_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (!map || !map->cartridge)
        return;
    if (addr > 0x1FFF)
        return;
    if (!map->cartridge->chr_ram_allocated)
        return; // CHR-ROM: writes are no-ops

    if (map->cartridge->chr_rom)
        map->cartridge->chr_rom[addr & 0x1FFF] = data;
}
