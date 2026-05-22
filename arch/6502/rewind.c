/*
 * rewind.c — frame-level rewind using a circular buffer of in-memory snapshots.
 *
 * Each call to rewind_push() serialises the full NES state (CPU, PPU, APU,
 * RAM, mapper, CHR-RAM) via savestate_save_mem() and stores it in the next
 * ring buffer slot, evicting the oldest slot if the buffer is full.
 *
 * rewind_step() walks the ring buffer backward one slot per call, restoring
 * the state via savestate_load_mem().
 */

#include "rewind.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "savestate.h"

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

struct rewind_buffer *rewind_init(void) {
    struct rewind_buffer *rb =
        (struct rewind_buffer *)calloc(1, sizeof(struct rewind_buffer));
    return rb;
}

void rewind_free(struct rewind_buffer *rb) {
    if (!rb) return;
    for (int i = 0; i < REWIND_SLOTS; i++) {
        if (rb->slots[i].data) {
            free(rb->slots[i].data);
            rb->slots[i].data = NULL;
        }
    }
    free(rb);
}

/* -------------------------------------------------------------------------
 * Push — capture current state into write_head, advance write_head
 * ---------------------------------------------------------------------- */

void rewind_push(struct rewind_buffer *rb, struct nesbus *bus) {
    if (!rb || !bus) return;

    int idx = rb->write_head;

    /* Free any previously stored snapshot in this slot (overwrite oldest). */
    if (rb->slots[idx].data) {
        free(rb->slots[idx].data);
        rb->slots[idx].data = NULL;
        rb->slots[idx].size = 0;
    }

    size_t   sz  = 0;
    uint8_t *buf = savestate_save_mem(bus, &sz);
    if (!buf) {
        fprintf(stderr, "rewind_push: savestate_save_mem failed\n");
        return;
    }

    rb->slots[idx].data = buf;
    rb->slots[idx].size = sz;

    rb->write_head = (idx + 1) % REWIND_SLOTS;

    if (rb->count < REWIND_SLOTS)
        rb->count++;

    /* Keep read_head pointing one step behind write_head so that the first
     * rewind_step() call restores the frame that was just pushed (i.e. the
     * previous frame relative to the current live state). */
    rb->read_head = (rb->write_head - 1 + REWIND_SLOTS) % REWIND_SLOTS;
}

/* -------------------------------------------------------------------------
 * Step — restore previous frame, return 1 on success, 0 if no history
 * ---------------------------------------------------------------------- */

int rewind_step(struct rewind_buffer *rb, struct nesbus *bus) {
    if (!rb || !bus) return 0;
    if (rb->count == 0) return 0;

    int idx = rb->read_head;

    if (!rb->slots[idx].data || rb->slots[idx].size == 0) return 0;

    int rc = savestate_load_mem(rb->slots[idx].data, rb->slots[idx].size, bus);
    if (rc != 0) {
        fprintf(stderr, "rewind_step: savestate_load_mem failed at slot %d\n", idx);
        return 0;
    }

    /* Free this slot — once consumed during rewind it is no longer needed,
     * and the space can be reclaimed so future pushes can reuse it. */
    free(rb->slots[idx].data);
    rb->slots[idx].data = NULL;
    rb->slots[idx].size = 0;

    rb->count--;

    /* Advance read_head backward (wrapping). */
    rb->read_head = (idx - 1 + REWIND_SLOTS) % REWIND_SLOTS;

    /* Pull write_head back so that resuming normal play overwrites from here. */
    rb->write_head = (idx + 1) % REWIND_SLOTS;

    return 1;
}
