#include "gamepad.h"
#include "controller.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct gamepad_slot {
    SDL_GameController *handle;
    int                 joystick_index; /* device index at open time */
};

struct gamepad_context {
    struct gamepad_slot slots[GAMEPAD_MAX_PLAYERS];
};

struct gamepad_context *gamepad_init(void) {
    struct gamepad_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    for (int i = 0; i < GAMEPAD_MAX_PLAYERS; i++)
        ctx->slots[i].joystick_index = -1;
    return ctx;
}

void gamepad_free(struct gamepad_context *ctx) {
    if (!ctx) return;
    for (int i = 0; i < GAMEPAD_MAX_PLAYERS; i++) {
        if (ctx->slots[i].handle) {
            SDL_GameControllerClose(ctx->slots[i].handle);
            ctx->slots[i].handle = NULL;
        }
    }
    free(ctx);
}

/* Called once per frame. Re-scans connected joysticks so hot-plug works. */
void gamepad_refresh(struct gamepad_context *ctx) {
    if (!ctx) return;

    int n = SDL_NumJoysticks();

    /* Close handles for joystick indices that no longer exist. */
    for (int p = 0; p < GAMEPAD_MAX_PLAYERS; p++) {
        if (!ctx->slots[p].handle) continue;
        int idx = ctx->slots[p].joystick_index;
        if (idx >= n || !SDL_IsGameController(idx) ||
            !SDL_GameControllerGetAttached(ctx->slots[p].handle)) {
            printf("Gamepad P%d disconnected\n", p + 1);
            SDL_GameControllerClose(ctx->slots[p].handle);
            ctx->slots[p].handle        = NULL;
            ctx->slots[p].joystick_index = -1;
        }
    }

    /* Open newly connected controllers into free slots. */
    for (int i = 0; i < n && i < GAMEPAD_MAX_PLAYERS * 4; i++) {
        if (!SDL_IsGameController(i)) continue;

        /* Skip if already open in a slot. */
        int already = 0;
        for (int p = 0; p < GAMEPAD_MAX_PLAYERS; p++) {
            if (ctx->slots[p].joystick_index == i) { already = 1; break; }
        }
        if (already) continue;

        /* Find a free slot. */
        int free_slot = -1;
        for (int p = 0; p < GAMEPAD_MAX_PLAYERS; p++) {
            if (!ctx->slots[p].handle) { free_slot = p; break; }
        }
        if (free_slot < 0) break;

        SDL_GameController *gc = SDL_GameControllerOpen(i);
        if (!gc) continue;
        ctx->slots[free_slot].handle         = gc;
        ctx->slots[free_slot].joystick_index  = i;
        printf("Gamepad P%d connected: %s\n", free_slot + 1,
               SDL_GameControllerName(gc));
    }
}

uint8_t gamepad_get_buttons(const struct gamepad_context *ctx, int player) {
    if (!ctx || player < 0 || player >= GAMEPAD_MAX_PLAYERS) return 0;
    SDL_GameController *gc = ctx->slots[player].handle;
    if (!gc) return 0;

    uint8_t buttons = 0;

#define BTN(sdl_btn, nes_bit) \
    if (SDL_GameControllerGetButton(gc, sdl_btn)) buttons |= (nes_bit)

    BTN(SDL_CONTROLLER_BUTTON_A,          CONTROLLER_A);
    BTN(SDL_CONTROLLER_BUTTON_B,          CONTROLLER_B);
    /* X/Y act as alternate A/B so SNES-layout pads feel natural. */
    BTN(SDL_CONTROLLER_BUTTON_X,          CONTROLLER_B);
    BTN(SDL_CONTROLLER_BUTTON_Y,          CONTROLLER_A);
    BTN(SDL_CONTROLLER_BUTTON_START,      CONTROLLER_START);
    BTN(SDL_CONTROLLER_BUTTON_BACK,       CONTROLLER_SELECT);
    BTN(SDL_CONTROLLER_BUTTON_DPAD_UP,    CONTROLLER_UP);
    BTN(SDL_CONTROLLER_BUTTON_DPAD_DOWN,  CONTROLLER_DOWN);
    BTN(SDL_CONTROLLER_BUTTON_DPAD_LEFT,  CONTROLLER_LEFT);
    BTN(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, CONTROLLER_RIGHT);
#undef BTN

    /* Left analog stick → d-pad with dead zone. */
    Sint16 ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -GAMEPAD_AXIS_DEADZONE) buttons |= CONTROLLER_LEFT;
    if (ax >  GAMEPAD_AXIS_DEADZONE) buttons |= CONTROLLER_RIGHT;
    if (ay < -GAMEPAD_AXIS_DEADZONE) buttons |= CONTROLLER_UP;
    if (ay >  GAMEPAD_AXIS_DEADZONE) buttons |= CONTROLLER_DOWN;

    return buttons;
}

int gamepad_is_connected(const struct gamepad_context *ctx, int player) {
    if (!ctx || player < 0 || player >= GAMEPAD_MAX_PLAYERS) return 0;
    return ctx->slots[player].handle != NULL;
}
