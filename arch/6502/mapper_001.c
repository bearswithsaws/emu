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

// Initialize MMC1 state
static void mmc1_init(void) {
    memset(&mmc1, 0, sizeof(struct mmc1_state));
    // Power-up state: control register starts at 0x0C
    // (PRG mode 3: fix last bank, CHR mode 0: 8KB)
    mmc1.control = 0x0C;
    mmc1.prg_mode = 3;
    mmc1.chr_mode = 0;
    mmc1.mirroring = 0;
}

// Update cached values from control register
static void mmc1_update_control(void) {
    mmc1.mirroring = mmc1.control & 0x03;
    mmc1.prg_mode = (mmc1.control >> 2) & 0x03;
    mmc1.chr_mode = (mmc1.control >> 4) & 0x01;
}

// Handle serial write to MMC1
static void mmc1_write_register(struct mapper *map, uint16_t addr, uint8_t data) {
    // Check for reset (bit 7 set)
    if (data & 0x80) {
        mmc1.shift_register = 0;
        mmc1.write_count = 0;
        mmc1.control |= 0x0C;  // Set to mode 3 (fix last bank)
        mmc1_update_control();
        return;
    }

    // Shift in bit 0
    mmc1.shift_register = (mmc1.shift_register >> 1) | ((data & 0x01) << 4);
    mmc1.write_count++;

    // After 5 writes, update the target register
    if (mmc1.write_count == 5) {
        uint8_t register_value = mmc1.shift_register;

        // Determine which register to update based on address
        if (addr >= 0x8000 && addr <= 0x9FFF) {
            // Control register
            mmc1.control = register_value;
            mmc1_update_control();
        } else if (addr >= 0xA000 && addr <= 0xBFFF) {
            // CHR bank 0
            mmc1.chr_bank_0 = register_value;
        } else if (addr >= 0xC000 && addr <= 0xDFFF) {
            // CHR bank 1
            mmc1.chr_bank_1 = register_value;
        } else if (addr >= 0xE000 && addr <= 0xFFFF) {
            // PRG bank
            mmc1.prg_bank = register_value;
        }

        // Reset shift register
        mmc1.shift_register = 0;
        mmc1.write_count = 0;
    }
}

uint8_t mapper_001_cpu_read(struct mapper *map, uint16_t addr) {
    // PRG-ROM is mapped to $8000-$FFFF (32KB window)
    // Depending on PRG mode, different banks are selected

    uint32_t prg_rom_offset = 0;
    uint8_t num_banks = map->num_prg_rom;  // Each bank is 16KB

    if (mmc1.prg_mode == 0 || mmc1.prg_mode == 1) {
        // 32KB mode: Ignore low bit of PRG bank, map $8000-$FFFF to consecutive 16KB banks
        uint8_t bank = (mmc1.prg_bank >> 1) & 0x0F;
        bank = bank % num_banks;  // Wrap if exceeds available banks
        prg_rom_offset = (bank * 0x4000) + (addr & 0x7FFF);
    } else if (mmc1.prg_mode == 2) {
        // Fix first bank at $8000, switch second bank at $C000
        if (addr >= 0x8000 && addr <= 0xBFFF) {
            // First 16KB: bank 0
            prg_rom_offset = (addr & 0x3FFF);
        } else {
            // Second 16KB: switchable
            uint8_t bank = mmc1.prg_bank & 0x0F;
            bank = bank % num_banks;
            prg_rom_offset = (bank * 0x4000) + (addr & 0x3FFF);
        }
    } else {  // mode 3
        // Switch first bank at $8000, fix last bank at $C000
        if (addr >= 0x8000 && addr <= 0xBFFF) {
            // First 16KB: switchable
            uint8_t bank = mmc1.prg_bank & 0x0F;
            bank = bank % num_banks;
            prg_rom_offset = (bank * 0x4000) + (addr & 0x3FFF);
        } else {
            // Second 16KB: last bank
            prg_rom_offset = ((num_banks - 1) * 0x4000) + (addr & 0x3FFF);
        }
    }

    return map->cartridge->prg_rom[prg_rom_offset];
}

void mapper_001_cpu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    // CPU writes to $8000-$FFFF go to MMC1 registers (serial write interface)
    if (addr >= 0x8000) {
        // Initialize MMC1 state on first write
        static int initialized = 0;
        if (!initialized) {
            mmc1_init();
            initialized = 1;
        }

        mmc1_write_register(map, addr, data);
    }
}

uint8_t mapper_001_ppu_read(struct mapper *map, uint16_t addr) {
    // CHR-ROM/RAM is mapped to $0000-$1FFF (8KB window)
    // Depending on CHR mode, different banks are selected

    if (!map->cartridge->chr_rom) {
        return 0;  // No CHR-ROM/RAM
    }

    // For CHR-RAM (when chr_rom_size=0), treat as single 8KB bank
    // For CHR-ROM, use banking as normal
    uint32_t chr_rom_offset = 0;

    // If this is CHR-RAM (no ROM banks), ignore banking and use direct addressing
    if (map->num_chr_rom == 0 || map->cartridge->chr_ram_allocated) {
        // CHR-RAM: Direct addressing, no banking
        chr_rom_offset = addr & 0x1FFF;
    } else {
        // CHR-ROM: Use MMC1 banking
        uint8_t num_banks = map->num_chr_rom;  // Each bank is 4KB

        if (mmc1.chr_mode == 0) {
            // 8KB mode: Ignore low bit of CHR bank 0, map $0000-$1FFF to consecutive 4KB banks
            uint8_t bank = (mmc1.chr_bank_0 >> 1) & 0x1F;
            if (num_banks > 0) {
                bank = bank % (num_banks * 2);  // *2 because we're treating as 4KB banks
            }
            chr_rom_offset = (bank * 0x1000) + (addr & 0x1FFF);
        } else {
            // 4KB mode: Two separate 4KB banks
            if (addr >= 0x0000 && addr <= 0x0FFF) {
                // First 4KB: CHR bank 0
                uint8_t bank = mmc1.chr_bank_0 & 0x1F;
                if (num_banks > 0) {
                    bank = bank % (num_banks * 2);
                }
                chr_rom_offset = (bank * 0x1000) + (addr & 0x0FFF);
            } else {
                // Second 4KB: CHR bank 1
                uint8_t bank = mmc1.chr_bank_1 & 0x1F;
                if (num_banks > 0) {
                    bank = bank % (num_banks * 2);
                }
                chr_rom_offset = (bank * 0x1000) + (addr & 0x0FFF);
            }
        }
    }

    // Bounds check
    if (chr_rom_offset < map->cartridge->chr_rom_len) {
        return map->cartridge->chr_rom[chr_rom_offset];
    }

    return 0;
}

void mapper_001_ppu_write(struct mapper *map, uint16_t addr, uint8_t data) {
    // CHR-RAM writes (if cartridge has RAM instead of ROM)
    if (!map || !map->cartridge) {
        return;
    }

    // Only write if CHR-RAM was allocated (not ROM)
    if (!map->cartridge->chr_ram_allocated) {
        return;  // CHR-ROM is read-only
    }

    // For CHR-RAM, use same logic as read
    uint32_t chr_rom_offset = 0;

    // CHR-RAM: Direct addressing (no banking)
    if (map->num_chr_rom == 0 || map->cartridge->chr_ram_allocated) {
        chr_rom_offset = addr & 0x1FFF;
    } else {
        // CHR-ROM with banking (shouldn't write here, but handle it anyway)
        uint8_t num_banks = map->num_chr_rom;

        if (mmc1.chr_mode == 0) {
            uint8_t bank = (mmc1.chr_bank_0 >> 1) & 0x1F;
            if (num_banks > 0) {
                bank = bank % (num_banks * 2);
            }
            chr_rom_offset = (bank * 0x1000) + (addr & 0x1FFF);
        } else {
            if (addr >= 0x0000 && addr <= 0x0FFF) {
                uint8_t bank = mmc1.chr_bank_0 & 0x1F;
                if (num_banks > 0) {
                    bank = bank % (num_banks * 2);
                }
                chr_rom_offset = (bank * 0x1000) + (addr & 0x0FFF);
            } else {
                uint8_t bank = mmc1.chr_bank_1 & 0x1F;
                if (num_banks > 0) {
                    bank = bank % (num_banks * 2);
                }
                chr_rom_offset = (bank * 0x1000) + (addr & 0x0FFF);
            }
        }
    }

    // Bounds check and write
    if (map->cartridge->chr_rom && chr_rom_offset < map->cartridge->chr_rom_len) {
        map->cartridge->chr_rom[chr_rom_offset] = data;
    }
}