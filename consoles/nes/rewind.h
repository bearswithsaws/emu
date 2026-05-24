/*
 * rewind.h — frame-level rewind using a circular buffer of in-memory snapshots.
 *
 * Usage:
 *   struct rewind_buffer *rb = rewind_init();
 *
 *   // Each frame (when NOT rewinding):
 *   rewind_push(rb, bus);
 *
 *   // While rewind key is held:
 *   if (!rewind_step(rb, bus)) { }  // no more history
 *
 *   rewind_free(rb);
 */

#ifndef REWIND_H
#define REWIND_H

#include <stddef.h>
#include "bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 30 seconds of history at 60 fps */
#define REWIND_SLOTS 1800

struct rewind_slot {
    uint8_t *data;
    size_t   size;
};

struct rewind_buffer {
    struct rewind_slot slots[REWIND_SLOTS];
    int write_head;   /* next slot index to overwrite */
    int read_head;    /* current rewind position (slot to restore next) */
    int count;        /* number of valid slots in the buffer */
    int rewinding;    /* 1 while the rewind key is held */
};

/* Allocate and zero-initialise a rewind buffer. */
struct rewind_buffer *rewind_init(void);

/*
 * Capture the current NES state into the ring buffer.
 * Call once per PPU frame while NOT rewinding.
 */
void rewind_push(struct rewind_buffer *rb, struct nesbus *bus);

/*
 * Restore the previous frame's state from the ring buffer.
 * Returns 1 on success, 0 if there is no more history.
 * Call once per PPU frame while the rewind key is held.
 */
int rewind_step(struct rewind_buffer *rb, struct nesbus *bus);

/* Free all slot data and the buffer struct itself. */
void rewind_free(struct rewind_buffer *rb);

#ifdef __cplusplus
}
#endif

#endif /* REWIND_H */
