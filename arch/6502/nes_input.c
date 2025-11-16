#include "nes_input.h"
#include <SDL2/SDL.h>
#include <stdio.h>

// Global pointer to NES controller (for callback access)
static struct controller *g_controller1 = NULL;

/**
 * NES input callback
 *
 * Maps SDL keyboard events to NES controller buttons.
 * Called by the display library for each key press/release.
 */
static void nes_input_handler(int sdl_keycode, int is_pressed, void *userdata) {
    (void)userdata; // Unused

    if (!g_controller1) {
        return; // Not initialized
    }

    uint8_t button = 0;

    // Map SDL keys to NES controller buttons
    switch (sdl_keycode) {
    // D-Pad
    case SDLK_UP:
        button = CONTROLLER_UP;
        break;
    case SDLK_DOWN:
        button = CONTROLLER_DOWN;
        break;
    case SDLK_LEFT:
        button = CONTROLLER_LEFT;
        break;
    case SDLK_RIGHT:
        button = CONTROLLER_RIGHT;
        break;

    // Action buttons
    case SDLK_z:
        button = CONTROLLER_A;
        break;
    case SDLK_x:
        button = CONTROLLER_B;
        break;

    // Start/Select
    case SDLK_RETURN:
        button = CONTROLLER_START;
        break;
    case SDLK_RSHIFT:
        button = CONTROLLER_SELECT;
        break;

    default:
        return; // Unknown key, ignore
    }

    // Update controller state
    controller_set_button(g_controller1, button, is_pressed);
}

void nes_input_init(struct controller *ctrl) {
    g_controller1 = ctrl;
    printf("NES input initialized\n");
}

input_callback_t nes_get_input_handler(void) { return nes_input_handler; }
