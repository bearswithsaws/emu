#ifndef __SAVESTATE_H__
#define __SAVESTATE_H__

#include <stddef.h>
#include "nesbus.h"

/*
 * Save/load the full emulator state to a numbered slot file.
 *
 * File location: $HOME/.local/share/emu/slot_N.sav  (fallback: ./slot_N.sav)
 *
 * Returns 0 on success, -1 on failure (prints reason to stderr).
 * Both functions pause audio writes on the bus APU ring before touching state.
 */
int savestate_save(int slot, struct nesbus *bus);
int savestate_load(int slot, struct nesbus *bus);

/* Write the save-file path for slot into buf (size len).  Returns buf. */
char *savestate_path(int slot, char *buf, size_t len);

#endif /* __SAVESTATE_H__ */
