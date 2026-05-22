#include "sram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

/*
 * Build the directory path ~/.local/share/emu and create it (and all parents)
 * if it does not already exist.  Writes the resulting directory path into
 * buf (up to buf_len bytes).  Returns 0 on success, -1 on error.
 */
static int ensure_sram_dir(char *buf, size_t buf_len) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home) return -1;

    /* Build path components incrementally and mkdir each one. */
    char tmp[512];
    const char *components[] = {
        "/.local",
        "/.local/share",
        "/.local/share/emu",
    };
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "%s%s", home, components[i]);
        /* Ignore EEXIST — we just want the directory to exist. */
        MKDIR(tmp);
    }

    snprintf(buf, buf_len, "%s/.local/share/emu", home);
    return 0;
}

/*
 * Derive the SRAM file path from rom_path.
 * e.g. /path/to/zelda.nes -> ~/.local/share/emu/zelda.sram
 * Writes the result into out (up to out_len bytes).
 * Returns 0 on success, -1 on error.
 */
static int sram_path(const char *rom_path, char *out, size_t out_len) {
    char dir[512];
    if (ensure_sram_dir(dir, sizeof(dir)) != 0)
        return -1;

    /* Extract basename from rom_path. */
    const char *slash = strrchr(rom_path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(rom_path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : rom_path;

    /* Strip .nes extension (case-insensitive). */
    char stem[256];
    snprintf(stem, sizeof(stem), "%s", base);
    size_t slen = strlen(stem);
    if (slen >= 4) {
        char *ext = stem + slen - 4;
        if (ext[0] == '.' &&
            (ext[1] == 'n' || ext[1] == 'N') &&
            (ext[2] == 'e' || ext[2] == 'E') &&
            (ext[3] == 's' || ext[3] == 'S')) {
            ext[0] = '\0';
        }
    }

    snprintf(out, out_len, "%s/%s.sram", dir, stem);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

int sram_load(struct nes_cartridge *cart, const char *rom_path) {
    if (!cart || !rom_path) return 0;
    if (!cart->has_battery) return 0;
    if (!cart->map || !cart->map->prg_ram || cart->map->prg_ram_size == 0)
        return 0;

    char path[512];
    if (sram_path(rom_path, path, sizeof(path)) != 0) {
        fprintf(stderr, "sram_load: could not determine SRAM path\n");
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        /* File does not exist yet — this is normal for a first run. */
        return 0;
    }

    size_t n = fread(cart->map->prg_ram, 1, cart->map->prg_ram_size, f);
    fclose(f);

    if (n != cart->map->prg_ram_size) {
        fprintf(stderr,
                "sram_load: expected %u bytes, got %zu from %s\n",
                cart->map->prg_ram_size, n, path);
        return -1;
    }

    printf("SRAM loaded from %s (%u bytes)\n", path, cart->map->prg_ram_size);
    return 1;
}

int sram_save(struct nes_cartridge *cart, const char *rom_path) {
    if (!cart || !rom_path) return 0;
    if (!cart->has_battery) return 0;
    if (!cart->map || !cart->map->prg_ram || cart->map->prg_ram_size == 0)
        return 0;

    char path[512];
    if (sram_path(rom_path, path, sizeof(path)) != 0) {
        fprintf(stderr, "sram_save: could not determine SRAM path\n");
        return -1;
    }

    /* Ensure the directory exists (may not if this is the very first save). */
    char dir[512];
    ensure_sram_dir(dir, sizeof(dir));

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "sram_save: cannot open %s for writing\n", path);
        return -1;
    }

    size_t n = fwrite(cart->map->prg_ram, 1, cart->map->prg_ram_size, f);
    fclose(f);

    if (n != cart->map->prg_ram_size) {
        fprintf(stderr,
                "sram_save: wrote %zu of %u bytes to %s\n",
                n, cart->map->prg_ram_size, path);
        return -1;
    }

    printf("SRAM saved to %s (%u bytes)\n", path, cart->map->prg_ram_size);
    return 1;
}
