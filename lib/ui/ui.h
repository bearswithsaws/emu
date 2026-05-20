#ifndef UI_H
#define UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct display_context;
struct ui_context;
struct cpu6502;
struct ppu2c02;
struct nesbus;

#define UI_MAX_BREAKPOINTS 16

typedef enum {
    UI_BP_EXEC  = 0,
    UI_BP_READ  = 1,
    UI_BP_WRITE = 2
} ui_bp_type;

struct ui_breakpoint {
    uint16_t  addr;
    ui_bp_type type;
    int       enabled;
};

/**
 * Callbacks provided by the emulator core so the menu bar can trigger
 * actions that require access to CPU/bus/cartridge state.
 */
typedef void (*ui_reset_fn)(void *userdata);
typedef void (*ui_load_rom_fn)(const char *path, void *userdata);
typedef void (*ui_savestate_fn)(int slot, void *userdata);

struct ui_callbacks {
    ui_reset_fn     on_soft_reset;   /* CPU reset, RAM preserved */
    ui_reset_fn     on_power_cycle;  /* Full hardware reinit */
    ui_load_rom_fn  on_load_rom;     /* Load a new ROM file by path */
    ui_savestate_fn on_save_state;   /* Save state to numbered slot */
    ui_savestate_fn on_load_state;   /* Load state from numbered slot */
    void           *userdata;
};

/**
 * Initialize Dear ImGui with the SDL2 + SDL_Renderer backend.
 * Must be called after display_init().
 * callbacks may be NULL (menu actions that need them will be no-ops).
 */
struct ui_context *ui_init(struct display_context *display,
                           const struct ui_callbacks *callbacks);

/**
 * Render one frame: uploads the NES framebuffer, runs ImGui, presents.
 * Replaces display_render_frame() in the main loop.
 */
void ui_render_frame(struct ui_context *ui, struct display_context *display);

/**
 * Shut down ImGui and free the context.
 */
void ui_shutdown(struct ui_context *ui);

/**
 * Toggle the ImGui demo window (useful during development).
 */
void ui_toggle_demo(struct ui_context *ui);

/**
 * Query the speed multiplier the user has selected via the Emulation menu.
 * Returns: 0.5, 1.0, 2.0, or -1.0 (uncapped).
 */
float ui_get_speed_multiplier(const struct ui_context *ui);

/**
 * Return non-zero if the FPS overlay is enabled.
 */
int ui_show_fps(const struct ui_context *ui);

/**
 * Notify the UI that a ROM has been loaded (loaded=1) or unloaded (loaded=0).
 * Controls whether the no-ROM splash screen is shown.
 */
void ui_notify_rom_loaded(struct ui_context *ui, int loaded);

/**
 * Provide live CPU/PPU/bus pointers for the CPU debugger panel.
 * Must be called after ui_init(). Pointers are held by reference only.
 */
void ui_set_debug_context(struct ui_context *ui,
                          struct cpu6502 *cpu,
                          struct ppu2c02 *ppu,
                          struct nesbus  *bus);

/**
 * Update the total CPU cycle counter shown in the debugger panel.
 * Call once per emulated frame.
 */
void ui_debugger_update_cycles(struct ui_context *ui, uint64_t cycles);

/**
 * Returns 1 and clears the flag if the user requested a single-instruction step.
 */
int ui_debugger_consume_step(struct ui_context *ui);

/**
 * Returns 1 and clears the flag if the user requested step-over.
 * *out_target is set to the address to run until (current PC + insn length).
 */
int ui_debugger_consume_step_over(struct ui_context *ui, uint16_t *out_target);

/**
 * Returns 1 and clears the flag if the user requested a break (pause).
 */
int ui_debugger_consume_break(struct ui_context *ui);

/**
 * Returns 1 if addr has an enabled EXEC breakpoint, 0 otherwise.
 */
int ui_debugger_is_breakpoint(struct ui_context *ui, uint16_t addr);

/**
 * Returns 1 and clears the flag if a read/write breakpoint was hit since the
 * last call.  The main loop should call this after each emu_tick() and pause.
 */
int ui_debugger_consume_rw_bp_hit(struct ui_context *ui);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
