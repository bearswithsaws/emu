#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <stdint.h>

/**
 * Generic SDL-based display library for emulators
 *
 * This library provides a reusable SDL2 display interface that can be used
 * by any pixel-based emulator (NES, Game Boy, SNES, etc.).
 *
 * Features:
 * - Configurable screen resolution and scaling
 * - Frame buffer interface (emulator writes pixels, display renders)
 * - Generic input handling via callbacks
 * - Pause/quit state management
 * - VSync support
 */

// Display configuration
struct display_config {
    const char *window_title; // Window title bar text
    int screen_width;         // Native screen width in pixels
    int screen_height;        // Native screen height in pixels
    int scale_factor; // Integer scaling factor (1=native, 2=2x, 3=3x, etc.)
    int enable_vsync; // 1=enable vsync, 0=disable
};

// Opaque display context (implementation hidden)
struct display_context;

/**
 * Input event callback
 *
 * Called when a keyboard event occurs. The emulator defines this callback
 * to map keys to emulator-specific inputs (controller buttons, etc.)
 *
 * @param sdl_keycode - SDL keycode (SDLK_*)
 * @param is_pressed  - 1 if key pressed, 0 if released
 * @param userdata    - User-provided context pointer
 */
typedef void (*input_callback_t)(int sdl_keycode, int is_pressed,
                                 void *userdata);

/**
 * Initialize the display system
 *
 * @param config - Display configuration
 * @return Display context on success, NULL on failure
 */
struct display_context *display_init(const struct display_config *config);

/**
 * Cleanup and destroy display
 *
 * @param ctx - Display context
 */
void display_cleanup(struct display_context *ctx);

/**
 * Get pointer to frame buffer
 *
 * Returns a pointer to the frame buffer where the emulator should write
 * pixels. Format is ARGB8888 (32 bits per pixel).
 *
 * @param ctx - Display context
 * @return Pointer to frame buffer (screen_width * screen_height * 4 bytes)
 */
uint32_t *display_get_framebuffer(struct display_context *ctx);

/**
 * Poll and process input events
 *
 * Processes SDL events (keyboard, window close, etc.). Built-in hotkeys:
 * - ESC: Quit
 * - SPACE: Toggle pause
 *
 * Other keys are passed to the input_callback for emulator-specific handling.
 *
 * @param ctx           - Display context
 * @param input_handler - Callback for input events (NULL = no callback)
 * @param userdata      - User context passed to callback
 * @return 1 if user wants to quit, 0 to continue
 */
int display_poll_events(struct display_context *ctx,
                        input_callback_t input_handler, void *userdata);

/**
 * Render current frame buffer to screen
 *
 * Updates the screen with the current frame buffer contents.
 * Call this once per frame after emulator has finished rendering.
 *
 * @param ctx - Display context
 */
void display_render_frame(struct display_context *ctx);

/**
 * Check if display is still running
 *
 * @param ctx - Display context
 * @return 1 if running, 0 if quit requested
 */
int display_is_running(struct display_context *ctx);

/**
 * Check if emulation is paused
 *
 * @param ctx - Display context
 * @return 1 if paused, 0 if running
 */
int display_is_paused(struct display_context *ctx);

/**
 * Set pause state
 *
 * @param ctx    - Display context
 * @param paused - 1 to pause, 0 to resume
 */
void display_set_paused(struct display_context *ctx, int paused);

#endif /* __DISPLAY_H__ */
