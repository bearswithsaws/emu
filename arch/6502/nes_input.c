#include "nes_input.h"
#include "debug.h"
#include <SDL2/SDL.h>

static struct controller *g_controller1 = NULL;
static struct controller *g_controller2 = NULL;

static void nes_input_handler(int sdl_keycode, int is_pressed, void *userdata) {
    (void)userdata;

    struct controller *ctrl = NULL;
    uint8_t button = 0;

    switch (sdl_keycode) {
    // Player 1: arrow keys + Z/X/Enter/RShift
    case SDLK_UP:       ctrl = g_controller1; button = CONTROLLER_UP;     break;
    case SDLK_DOWN:     ctrl = g_controller1; button = CONTROLLER_DOWN;   break;
    case SDLK_LEFT:     ctrl = g_controller1; button = CONTROLLER_LEFT;   break;
    case SDLK_RIGHT:    ctrl = g_controller1; button = CONTROLLER_RIGHT;  break;
    case SDLK_z:        ctrl = g_controller1; button = CONTROLLER_A;      break;
    case SDLK_x:        ctrl = g_controller1; button = CONTROLLER_B;      break;
    case SDLK_RETURN:   ctrl = g_controller1; button = CONTROLLER_START;  break;
    case SDLK_RSHIFT:   ctrl = g_controller1; button = CONTROLLER_SELECT; break;

    // Player 2: WASD + N/M/Y/H
    case SDLK_w:        ctrl = g_controller2; button = CONTROLLER_UP;     break;
    case SDLK_s:        ctrl = g_controller2; button = CONTROLLER_DOWN;   break;
    case SDLK_a:        ctrl = g_controller2; button = CONTROLLER_LEFT;   break;
    case SDLK_d:        ctrl = g_controller2; button = CONTROLLER_RIGHT;  break;
    case SDLK_n:        ctrl = g_controller2; button = CONTROLLER_A;      break;
    case SDLK_m:        ctrl = g_controller2; button = CONTROLLER_B;      break;
    case SDLK_y:        ctrl = g_controller2; button = CONTROLLER_START;  break;
    case SDLK_h:        ctrl = g_controller2; button = CONTROLLER_SELECT; break;

    default:
        return;
    }

    if (ctrl) {
        controller_set_button(ctrl, button, is_pressed);
    }
}

void nes_input_init(struct controller *ctrl1, struct controller *ctrl2) {
    g_controller1 = ctrl1;
    g_controller2 = ctrl2;
    LOG_DEBUG("NES input initialized\n");
}

input_callback_t nes_get_input_handler(void) { return nes_input_handler; }

void nes_input_refresh(void) {
    if (!g_controller1 || !g_controller2) return;
    const uint8_t *k = SDL_GetKeyboardState(NULL);

    g_controller1->buttons =
        ((k[SDL_SCANCODE_UP])     ? CONTROLLER_UP     : 0) |
        ((k[SDL_SCANCODE_DOWN])   ? CONTROLLER_DOWN   : 0) |
        ((k[SDL_SCANCODE_LEFT])   ? CONTROLLER_LEFT   : 0) |
        ((k[SDL_SCANCODE_RIGHT])  ? CONTROLLER_RIGHT  : 0) |
        ((k[SDL_SCANCODE_Z])      ? CONTROLLER_A      : 0) |
        ((k[SDL_SCANCODE_X])      ? CONTROLLER_B      : 0) |
        ((k[SDL_SCANCODE_RETURN]) ? CONTROLLER_START  : 0) |
        ((k[SDL_SCANCODE_RSHIFT]) ? CONTROLLER_SELECT : 0);

    g_controller2->buttons =
        ((k[SDL_SCANCODE_W]) ? CONTROLLER_UP     : 0) |
        ((k[SDL_SCANCODE_S]) ? CONTROLLER_DOWN   : 0) |
        ((k[SDL_SCANCODE_A]) ? CONTROLLER_LEFT   : 0) |
        ((k[SDL_SCANCODE_D]) ? CONTROLLER_RIGHT  : 0) |
        ((k[SDL_SCANCODE_N]) ? CONTROLLER_A      : 0) |
        ((k[SDL_SCANCODE_M]) ? CONTROLLER_B      : 0) |
        ((k[SDL_SCANCODE_Y]) ? CONTROLLER_START  : 0) |
        ((k[SDL_SCANCODE_H]) ? CONTROLLER_SELECT : 0);
}
