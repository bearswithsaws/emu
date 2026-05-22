#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <stdint.h>

/*
 * SDL2 gamepad support for up to 2 controllers.
 *
 * Uses the SDL_GameController polling API so button state is always current.
 * Hot-plug is handled by re-scanning SDL_NumJoysticks() each frame inside
 * gamepad_refresh(). Analog left-stick input maps to d-pad with a dead zone.
 *
 * NES button bitmask returned by gamepad_get_buttons() uses the same bit
 * positions as controller.h (CONTROLLER_A, CONTROLLER_B, etc.).
 */

#define GAMEPAD_MAX_PLAYERS 2
#define GAMEPAD_AXIS_DEADZONE 8000  /* SDL axis range is +-32767 (~24%) */

struct gamepad_context;

/* Allocate and return a new gamepad context. Call after SDL_Init(). */
struct gamepad_context *gamepad_init(void);

/* Free the context and close any open controller handles. */
void gamepad_free(struct gamepad_context *ctx);

/*
 * Scan for newly connected/disconnected controllers and update handles.
 * Call once per frame before reading button state.
 */
void gamepad_refresh(struct gamepad_context *ctx);

/*
 * Return the current NES button bitmask for the given player (0 or 1).
 * Bits match controller.h definitions (A|B|Select|Start|Up|Down|Left|Right).
 * Returns 0 if no controller is connected for that player slot.
 */
uint8_t gamepad_get_buttons(const struct gamepad_context *ctx, int player);

/* Returns 1 if a controller is connected for the given player slot. */
int gamepad_is_connected(const struct gamepad_context *ctx, int player);

#endif /* GAMEPAD_H */
