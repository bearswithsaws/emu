/*
 * tas.c — Tool-Assisted Speedrun (TAS) input recording and playback
 */

#include "tas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * File format constants
 * ---------------------------------------------------------------------- */

#define TAS_MAGIC   "TASR"
#define TAS_VERSION 1u

/* Header layout (16 bytes total) */
typedef struct {
    char     magic[4];    /* "TASR" */
    uint32_t version;     /* 1 — little-endian */
    uint8_t  reserved[8]; /* zero */
} tas_header_t;

/* Per-frame record (6 bytes) */
typedef struct {
    uint32_t frame;
    uint8_t  p1;
    uint8_t  p2;
} tas_frame_t;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void write_u32le(FILE *f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v & 0xFF),
        (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 24) & 0xFF),
    };
    fwrite(b, 1, 4, f);
}

static int read_u32le(FILE *f, uint32_t *out) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *out = (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 0;
}

/* Write the 16-byte file header. */
static void write_header(FILE *f) {
    fwrite(TAS_MAGIC, 1, 4, f);
    write_u32le(f, TAS_VERSION);
    uint8_t reserved[8] = {0};
    fwrite(reserved, 1, 8, f);
}

/* Read and validate the 16-byte file header.
 * Returns 0 on success, -1 on error. */
static int read_header(FILE *f) {
    char magic[4];
    if (fread(magic, 1, 4, f) != 4) return -1;
    if (memcmp(magic, TAS_MAGIC, 4) != 0) {
        fprintf(stderr, "tas: bad magic — not a .tasr file\n");
        return -1;
    }
    uint32_t version;
    if (read_u32le(f, &version) != 0) return -1;
    if (version != TAS_VERSION) {
        fprintf(stderr, "tas: unsupported version %u (expected %u)\n",
                version, TAS_VERSION);
        return -1;
    }
    /* Skip 8 reserved bytes. */
    uint8_t reserved[8];
    if (fread(reserved, 1, 8, f) != 8) return -1;
    return 0;
}

/* Write one frame record. */
static void write_frame(FILE *f, uint32_t frame, uint8_t p1, uint8_t p2) {
    write_u32le(f, frame);
    fputc(p1, f);
    fputc(p2, f);
}

/* Read one frame record into a tas_context.
 * Returns 0 on success, 1 on EOF, -1 on error. */
static int read_next_frame(struct tas_context *tas) {
    uint32_t frame;
    if (read_u32le(tas->file, &frame) != 0) {
        /* EOF or error — playback finished */
        return 1;
    }
    int p1 = fgetc(tas->file);
    int p2 = fgetc(tas->file);
    if (p1 == EOF || p2 == EOF) return 1;

    tas->frame   = frame;
    tas->last_p1 = (uint8_t)p1;
    tas->last_p2 = (uint8_t)p2;
    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

struct tas_context *tas_init(void) {
    struct tas_context *tas = calloc(1, sizeof(*tas));
    if (!tas) return NULL;
    tas->mode = TAS_IDLE;
    return tas;
}

void tas_free(struct tas_context *tas) {
    if (!tas) return;
    tas_stop(tas);
    free(tas);
}

int tas_start_record(struct tas_context *tas, const char *path) {
    if (!tas || !path) return -1;
    tas_stop(tas);

    tas->file = fopen(path, "wb");
    if (!tas->file) {
        fprintf(stderr, "tas: cannot open '%s' for writing\n", path);
        return -1;
    }
    write_header(tas->file);
    fflush(tas->file);

    tas->mode  = TAS_RECORDING;
    tas->frame = 0;
    return 0;
}

void tas_stop(struct tas_context *tas) {
    if (!tas) return;
    if (tas->file) {
        fclose(tas->file);
        tas->file = NULL;
    }
    tas->mode    = TAS_IDLE;
    tas->frame   = 0;
    tas->last_p1 = 0;
    tas->last_p2 = 0;
}

int tas_start_play(struct tas_context *tas, const char *path) {
    if (!tas || !path) return -1;
    tas_stop(tas);

    tas->file = fopen(path, "rb");
    if (!tas->file) {
        fprintf(stderr, "tas: cannot open '%s' for reading\n", path);
        return -1;
    }
    if (read_header(tas->file) != 0) {
        fclose(tas->file);
        tas->file = NULL;
        return -1;
    }

    tas->mode  = TAS_PLAYING;
    tas->frame = 0;

    /* Pre-read first frame so tas_get_buttons() is valid on frame 0. */
    if (read_next_frame(tas) != 0) {
        /* Empty file — stop immediately. */
        fprintf(stderr, "tas: .tasr file has no frame records\n");
        tas_stop(tas);
        return -1;
    }
    return 0;
}

int tas_tick(struct tas_context *tas, uint8_t p1, uint8_t p2) {
    if (!tas) return 0;

    switch (tas->mode) {
    case TAS_RECORDING:
        write_frame(tas->file, tas->frame, p1, p2);
        fflush(tas->file);
        tas->frame++;
        return 0;

    case TAS_PLAYING: {
        /* Advance to the next frame record. */
        int ret = read_next_frame(tas);
        if (ret != 0) {
            /* EOF — playback finished; return 1 to signal done. */
            tas_stop(tas);
            return 1;
        }
        tas->frame++;
        return 0;
    }

    case TAS_IDLE:
    default:
        return 0;
    }
}

void tas_get_buttons(struct tas_context *tas, uint8_t *p1, uint8_t *p2) {
    if (p1) *p1 = 0;
    if (p2) *p2 = 0;
    if (!tas || tas->mode != TAS_PLAYING) return;
    if (p1) *p1 = tas->last_p1;
    if (p2) *p2 = tas->last_p2;
}

tas_mode_t tas_get_mode(struct tas_context *tas) {
    return tas ? tas->mode : TAS_IDLE;
}

uint32_t tas_get_frame(struct tas_context *tas) {
    return tas ? tas->frame : 0;
}
