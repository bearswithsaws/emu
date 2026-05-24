/*
 * tas.h — Tool-Assisted Speedrun (TAS) input recording and playback
 *
 * Records NES controller inputs frame-by-frame to a .tasr file and
 * plays them back deterministically.
 *
 * File format (.tasr):
 *   Header (16 bytes):
 *     Magic:    "TASR" (4 bytes)
 *     Version:  uint32_t = 1  (little-endian)
 *     Reserved: 8 bytes (zero-filled)
 *
 *   Frame records (repeated until EOF):
 *     frame:  uint32_t   (0-based frame counter, little-endian)
 *     p1:     uint8_t    (Player 1 button bitmask)
 *     p2:     uint8_t    (Player 2 button bitmask)
 */

#ifndef TAS_H
#define TAS_H

#include <stdint.h>
#include <stdio.h>

typedef enum {
    TAS_IDLE      = 0,
    TAS_RECORDING = 1,
    TAS_PLAYING   = 2
} tas_mode_t;

struct tas_context {
    tas_mode_t mode;
    FILE      *file;
    uint32_t   frame;
    uint8_t    last_p1;   /* playback: current frame's P1 buttons */
    uint8_t    last_p2;   /* playback: current frame's P2 buttons */
};

/**
 * Allocate and initialise a TAS context (mode = TAS_IDLE).
 */
struct tas_context *tas_init(void);

/**
 * Free a TAS context. Stops any active recording/playback first.
 */
void tas_free(struct tas_context *tas);

/**
 * Begin recording to the given file path.
 * Writes the 16-byte header and resets the frame counter.
 * Returns 0 on success, -1 on error.
 */
int tas_start_record(struct tas_context *tas, const char *path);

/**
 * Stop recording (or playback) and close the file.
 */
void tas_stop(struct tas_context *tas);

/**
 * Begin playback from the given file path.
 * Validates the header and pre-reads the first frame record.
 * Returns 0 on success, -1 on error (file not found, bad magic, etc.).
 */
int tas_start_play(struct tas_context *tas, const char *path);

/**
 * Call once per PPU frame.
 *
 * Recording mode: writes a frame record with the supplied p1/p2 bitmasks.
 * Playback mode:  reads the next frame record into last_p1/last_p2.
 *                 Returns 1 when EOF is reached (playback done), 0 otherwise.
 * Idle mode:      no-op, returns 0.
 */
int tas_tick(struct tas_context *tas, uint8_t p1, uint8_t p2);

/**
 * In playback mode, return the button states for the current frame.
 * In all other modes, returns 0 for both.
 */
void tas_get_buttons(struct tas_context *tas, uint8_t *p1, uint8_t *p2);

/** Return the current mode. */
tas_mode_t tas_get_mode(struct tas_context *tas);

/** Return the current frame counter. */
uint32_t tas_get_frame(struct tas_context *tas);

#endif /* TAS_H */
