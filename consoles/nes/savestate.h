#ifndef __SAVESTATE_H__
#define __SAVESTATE_H__

#include <stddef.h>
#include <stdint.h>
#include "bus.h"

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

/*
 * In-memory save/load — used by the rewind subsystem.
 *
 * savestate_save_mem: Serialise full NES state into a malloc'd buffer.
 *   Returns the buffer pointer (caller must free()) and writes its size to
 *   *out_size.  Returns NULL on failure.
 *
 * savestate_load_mem: Deserialise from a buffer.  Returns 0 on success,
 *   -1 on failure.
 */
uint8_t *savestate_save_mem(struct nesbus *bus, size_t *out_size);
int      savestate_load_mem(const uint8_t *buf, size_t size,
                            struct nesbus *bus);

#endif /* __SAVESTATE_H__ */
