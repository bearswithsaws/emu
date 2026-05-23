#include "gamepad.h"
#include "controller.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct gamepad_action_map k_default_action_map = {
    .entries = {
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  GAMEPAD_ACTION_REWIND       },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GAMEPAD_ACTION_FAST_FORWARD },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
    }
};

struct gamepad_slot {
    SDL_GameController *handle;
    SDL_JoystickID      instance_id; /* stable ID, does not shift on disconnect */
};

struct gamepad_context {
    struct gamepad_slot    slots[GAMEPAD_MAX_PLAYERS];
    struct gamepad_action_map action_map;
    uint32_t               prev_actions[GAMEPAD_MAX_PLAYERS];
};

struct gamepad_context *gamepad_init(void) {
    struct gamepad_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    for (int i = 0; i < GAMEPAD_MAX_PLAYERS; i++)
        ctx->slots[i].instance_id = -1;
    ctx->action_map = k_default_action_map;
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

void gamepad_refresh(struct gamepad_context *ctx) {
    if (!ctx) return;

    /* Close slots whose controllers are no longer attached.
     * Use SDL_GameControllerGetAttached() — reliable regardless of how SDL
     * renumbers device indices after disconnections. */
    for (int p = 0; p < GAMEPAD_MAX_PLAYERS; p++) {
        if (!ctx->slots[p].handle) continue;
        if (!SDL_GameControllerGetAttached(ctx->slots[p].handle)) {
            printf("Gamepad P%d disconnected\n", p + 1);
            SDL_GameControllerClose(ctx->slots[p].handle);
            ctx->slots[p].handle      = NULL;
            ctx->slots[p].instance_id = -1;
        }
    }

    /* Open newly connected controllers into free slots.
     * Compare via SDL_JoystickID (stable) to avoid opening the same physical
     * device twice after SDL renumbers indices on disconnect. */
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (!SDL_IsGameController(i)) continue;

        SDL_JoystickID new_id = SDL_JoystickGetDeviceInstanceID(i);

        int already = 0;
        for (int p = 0; p < GAMEPAD_MAX_PLAYERS; p++) {
            if (ctx->slots[p].handle && ctx->slots[p].instance_id == new_id) {
                already = 1;
                break;
            }
        }
        if (already) continue;

        int free_slot = -1;
        for (int p = 0; p < GAMEPAD_MAX_PLAYERS; p++) {
            if (!ctx->slots[p].handle) { free_slot = p; break; }
        }
        if (free_slot < 0) break;

        SDL_GameController *gc = SDL_GameControllerOpen(i);
        if (!gc) continue;
        ctx->slots[free_slot].handle      = gc;
        ctx->slots[free_slot].instance_id = new_id;
        printf("Gamepad P%d connected: %s\n", free_slot + 1,
               SDL_GameControllerName(gc));
    }
}

uint8_t gamepad_get_buttons(const struct gamepad_context *ctx, int player) {
    if (!ctx || player < 0 || player >= GAMEPAD_MAX_PLAYERS) return 0;
    SDL_GameController *gc = ctx->slots[player].handle;
    if (!gc) return 0;

    /* Buttons that are bound to emulator actions are excluded here so they
     * don't accidentally register as NES inputs. */
    uint32_t action_btns = 0;
    for (int i = 0; i < GAMEPAD_ACTION_MAP_SIZE; i++) {
        if (ctx->action_map.entries[i].action)
            action_btns |= (1u << ctx->action_map.entries[i].button_id);
    }

    uint8_t buttons = 0;

#define BTN(sdl_btn, nes_bit) \
    if (!((1u << (sdl_btn)) & action_btns) && \
        SDL_GameControllerGetButton(gc, sdl_btn)) buttons |= (nes_bit)

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

uint32_t gamepad_get_emulator_actions(const struct gamepad_context *ctx, int player) {
    if (!ctx || player < 0 || player >= GAMEPAD_MAX_PLAYERS) return 0;
    SDL_GameController *gc = ctx->slots[player].handle;
    if (!gc) return 0;

    uint32_t actions = 0;
    for (int i = 0; i < GAMEPAD_ACTION_MAP_SIZE; i++) {
        const struct gamepad_action_entry *e = &ctx->action_map.entries[i];
        if (!e->action) continue;
        if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)e->button_id))
            actions |= e->action;
    }
    return actions;
}

void gamepad_set_action_map(struct gamepad_context *ctx,
                            const struct gamepad_action_map *map) {
    if (!ctx) return;
    ctx->action_map = map ? *map : k_default_action_map;
}

void gamepad_get_action_map(const struct gamepad_context *ctx,
                            struct gamepad_action_map *out_map) {
    if (!ctx || !out_map) return;
    *out_map = ctx->action_map;
}

uint32_t gamepad_consume_action_edges(struct gamepad_context *ctx, int player) {
    if (!ctx || player < 0 || player >= GAMEPAD_MAX_PLAYERS) return 0;
    uint32_t cur  = gamepad_get_emulator_actions(ctx, player);
    uint32_t prev = ctx->prev_actions[player];
    ctx->prev_actions[player] = cur;
    return cur & ~prev; /* bits that transitioned 0→1 this frame */
}

int gamepad_is_connected(const struct gamepad_context *ctx, int player) {
    if (!ctx || player < 0 || player >= GAMEPAD_MAX_PLAYERS) return 0;
    return ctx->slots[player].handle != NULL;
}
