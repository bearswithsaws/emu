#ifndef __SRAM_H__
#define __SRAM_H__

#include "cartridge.h"

/*
 * Battery-backed PRG-RAM (SRAM) persistence.
 *
 * The SRAM file path is: ~/.local/share/emu/<rom_basename>.sram
 * e.g. /path/to/zelda.nes -> ~/.local/share/emu/zelda.sram
 *
 * Only cartridges with has_battery set and a mapper that exposes prg_ram
 * are written/read.
 */

/*
 * Load SRAM from disk into the mapper's PRG-RAM if the cartridge has a
 * battery and the save file exists.  rom_path is the path originally used
 * to load the ROM (used to derive the .sram filename).
 * Returns 1 on success, 0 if not applicable or file not found, -1 on error.
 */
int sram_load(struct nes_cartridge *cart, const char *rom_path);

/*
 * Save the mapper's PRG-RAM to disk if the cartridge has a battery.
 * Returns 1 on success, 0 if not applicable, -1 on error.
 */
int sram_save(struct nes_cartridge *cart, const char *rom_path);

#endif /* __SRAM_H__ */
