#include "mapper_001.h"
#include <stdio.h>
#include <string.h>

// MMC1 (Mapper 1) Internal State
// MMC1 uses a serial write interface and has 4 internal registers
struct mmc1_state {
    uint8_t shift_register;  // 5-bit shift register for serial writes
    uint8_t write_count;     // Number of writes received (0-4)

    // Internal registers
    uint8_t control;         // Control register ($8000-$9FFF)
    uint8_t chr_bank_0;      // CHR bank 0 ($A000-$BFFF)
    uint8_t chr_bank_1;      // CHR bank 1 ($C000-$DFFF)
    uint8_t prg_bank;        // PRG bank ($E000-$FFFF)

    // Cached values from control register
    uint8_t mirroring;       // 0=one-screen lower, 1=one-screen upper, 2=vertical, 3=horizontal
    uint8_t prg_mode;        // 0/1=32KB mode, 2=fix first bank, 3=fix last bank
    uint8_t chr_mode;        // 0=8KB mode, 1=4KB mode
};

// Global MMC1 state (one per mapper instance)
// TODO: This should be stored in the mapper struct itself
static struct mmc1_state mmc1 = {0};

// 8 KB battery-backed PRG-RAM at $6000-$7FFF.
// Most MMC1 games (Zelda, Metroid, Mega Man 2) use this for save data / state.
static uint8_t prg_ram[0x2000] = {0};

// Translate MMC1 control-register mirroring bits to the MIRROR_* constants
// used by struct mapper so the PPU can pick them up dynamically.
// MMC1: 0=single-lo, 1=single-hi, 2=vertical, 3=horizontal
static uint8_t mmc1_to_mirror(uint8_t mmc1_mirror) {
    switch (mmc1_mirror) {
    case 0:  return MIRROR_SINGLE_LO;
    case 1:  return MIRROR_SINGLE_HI;
    case 2:  return MIRROR_VERTICAL;
    default: return MIRROR_HORIZONTAL;
    }
}

// Update cached values from control register
static void mmc1_update_control(struct mapper *map) {
    mmc1.mirroring = mmc1.control & 0x03;
    mmc1.prg_mode  = (mmc1.control >> 2) & 0x03;
    mmc1.chr_mode  = (mmc1.control >> 4) & 0x01;
    map->mirroring = mmc1_to_mirror(mmc1.mirroring);
}

// Initialize MMC1 state — called once at cartridge load (not lazily).
void mapper_001_init(struct mapper *map) {
    memset(&mmc1, 0, sizeof(struct mmc1_state));
    memset(prg_ram, 0, sizeof(prg_ram));

    // Power-up state: control register = $0C (PRG mode 3: fix last bank, CHR 8KB)
    mmc1.control  = 0x0C;
    mmc1_update_control(map);
}

// Handle serial write to MMC1
static void mmc1_write_register(struct mapper *map, uint16_t addr, uint8_t data) {
    // Bit 7 set = reset shift register
    if (data & 0x80) {
        mmc1.shift_register = 0;
        mmc1.write_count    = 0;
        mmc1.control |= 0x0C;
        mmc1_update_control(map);
        return;
    }

    // Shift bit 0 into the MSB of the 5-bit shift register
    mmc1.shift_register = (mmc1.shift_register >> 1) | ((data & 0x01) << 4);
    mmc1.write_count++;

    // After 5 writes, latch into the addressed register
    if (mmc1.write_count == 5) {
        uint8_t value = mmc1.shift_register;

        if (addr <= 0x9FFF) {
            mmc1.control = value;
            mmc1_update_control(map);
        } else if (addr <= 0xBFFF) {
            mmc1.chr_bank_0 = value;
        } else if (addr <= 0xDFFF) {
            mmc1.chr_bank_1 = value;
        } else {
            mmc1.prg_bank = value;
        }

        mmc1.shift_register = 0;
        mmc1.write_count    = 0;
    }
}

uint8_t mapper_001_cpu_read(struct mapper *map, uint16_t addr) {
    // PRG-RAM at $6000-$7FFF.
    // Bit 4 of prg_bank register disables PRG-RAM when set (open bus → 0xFF).
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (mmc1.prg_bank & 0x10) return 0xFF;  // PRG-RAM disabled
        return prg_ram[addr & 0x1FFF];
    }

    // PRG-ROM at $8000-$FFFF
    uint32_t prg_rom_offset = 0;
    uint8_t  num_banks      = map->num_prg_rom;  // each bank = 16 KB

    if (mmc1.prg_mode <= 1) {
        // 32 KB mode: ignore low bit of prg_bank; map $8000-$FFFF as one 32 KB block
        uint8_t bank    = (mmc1.prg_bank >> 1) & 0x0F;
        bank            = bank % num_banks;
        prg_rom_offset  = (bank * 0x4000) + (addr & 0x7FFF);
    } else if (mmc1.prg_mode == 2) {
        // Fix first bank at $8000, switch 16 KB at $C000
        if (addr <= 0xBFFF) {
            prg_rom_offset = addr & 0x3FFF;
        } else {
            uint8_t bank   = mmc1.prg_bank & 0x0F;
            bank           = bank % num_banks;
            prg_rom_offset = (bank * 0x4000) + (addr & 0x3FFF);
        }
    } else {
        // Fix last bank at $C000, switch 16 KB at $8000
        if (addr <= 0xBFFF) {
            uint8_t bank   = mmc1.prg_bank & 0x0F;
            bank           = bank % num_banks;
            prg_rom_offset = (bank * 0x4000) + (addr & 0x3FFF);
        } else {
            prg_rom_offset = ((num_banks - 1) * 0x4000) + (addr & 0x3FFF);
        }
    }

    return map->cartridge->prg_rom[prg_rom_offset];
}

void mapper_001_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    // PRG-RAM at $6000-$7FFF
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        if (!(mmc1.prg_bank & 0x10)) {  // write only when PRG-RAM is enabled
            prg_ram[addr & 0x1FFF] = data;
        }
        return;
    }

    // Serial register writes at $8000-$FFFF
    if (addr >= 0x8000) {
        mmc1_write_register(map, addr, data);
    }
}

uint8_t mapper_001_ppu_read(struct mapper *map, uint16_t addr) {
    if (!map->cartridge->chr_rom) return 0;

    uint32_t chr_rom_offset = 0;

    // CHR-RAM: single 8 KB bank, no banking
    if (map->num_chr_rom == 0 || map->cartridge->chr_ram_allocated) {
        chr_rom_offset = addr & 0x1FFF;
    } else {
        uint8_t num_banks = map->num_chr_rom;  // each bank = 8 KB in the header

        if (mmc1.chr_mode == 0) {
            // 8 KB mode: bit 0 of chr_bank_0 is ignored; select 8 KB page
            uint8_t bank   = (mmc1.chr_bank_0 >> 1) & 0x1F;
            bank           = bank % num_banks;
            chr_rom_offset = (bank * 0x2000) + (addr & 0x1FFF);
        } else {
            // 4 KB mode: two independent 4 KB banks
            // num_banks is in 8 KB units, so there are num_banks*2 4 KB pages
            if (addr <= 0x0FFF) {
                uint8_t bank   = mmc1.chr_bank_0 & 0x1F;
                bank           = bank % (num_banks * 2);
                chr_rom_offset = (bank * 0x1000) + (addr & 0x0FFF);
            } else {
                uint8_t bank   = mmc1.chr_bank_1 & 0x1F;
                bank           = bank % (num_banks * 2);
                chr_rom_offset = (bank * 0x1000) + (addr & 0x0FFF);
            }
        }
    }

    if (chr_rom_offset < map->cartridge->chr_rom_len) {
        return map->cartridge->chr_rom[chr_rom_offset];
    }

    return 0;
}

void mapper_001_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    if (!map || !map->cartridge) return;

    // CHR-ROM is read-only; only write to CHR-RAM
    if (!map->cartridge->chr_ram_allocated) return;

    uint32_t chr_rom_offset = addr & 0x1FFF;  // CHR-RAM: direct addressing

    if (map->cartridge->chr_rom && chr_rom_offset < map->cartridge->chr_rom_len) {
        map->cartridge->chr_rom[chr_rom_offset] = data;
    }
}
