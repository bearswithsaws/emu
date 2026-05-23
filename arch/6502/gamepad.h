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
 *
 * Extra controller buttons (shoulders, triggers) trigger emulator actions
 * returned by gamepad_get_emulator_actions(). These are independent of NES
 * button state and can be configured via gamepad_set_action_map().
 */

#define GAMEPAD_MAX_PLAYERS   2
#define GAMEPAD_AXIS_DEADZONE 8000  /* SDL axis range +-32767 (~24%) */

/* Emulator action flags returned by gamepad_get_emulator_actions(). */
#define GAMEPAD_ACTION_REWIND        (1 << 0)
#define GAMEPAD_ACTION_FAST_FORWARD  (1 << 1)
#define GAMEPAD_ACTION_SAVE_STATE    (1 << 2)
#define GAMEPAD_ACTION_LOAD_STATE    (1 << 3)
#define GAMEPAD_ACTION_SCREENSHOT    (1 << 4)

/*
 * Maps a single SDL button index to an emulator action flag.
 * Use SDL_CONTROLLER_BUTTON_* constants for button_id.
 * Set action to 0 to disable that slot.
 */
struct gamepad_action_entry {
    int      button_id; /* SDL_GameControllerButton value */
    uint32_t action;    /* GAMEPAD_ACTION_* flag, or 0 */
};

#define GAMEPAD_ACTION_MAP_SIZE 8

struct gamepad_action_map {
    struct gamepad_action_entry entries[GAMEPAD_ACTION_MAP_SIZE];
};

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

/*
 * Return a bitmask of GAMEPAD_ACTION_* flags for buttons pressed on the
 * given player's controller that are bound to emulator actions.
 * These buttons do NOT appear in gamepad_get_buttons().
 */
uint32_t gamepad_get_emulator_actions(const struct gamepad_context *ctx, int player);

/*
 * Replace the action map for all players. Pass NULL to restore defaults.
 * The default map is:
 *   LEFTSHOULDER  → GAMEPAD_ACTION_REWIND
 *   RIGHTSHOULDER → GAMEPAD_ACTION_FAST_FORWARD
 */
void gamepad_set_action_map(struct gamepad_context *ctx,
                            const struct gamepad_action_map *map);

/* Returns 1 if a controller is connected for the given player slot. */
int gamepad_is_connected(const struct gamepad_context *ctx, int player);

/* Fill *map with the current action map. */
void gamepad_get_action_map(const struct gamepad_context *ctx,
                            struct gamepad_action_map *out_map);

/*
 * Returns only action bits that transitioned from not-pressed to pressed this
 * frame (rising edge), then clears them. Use for one-shot actions like save
 * state or screenshot so holding the button fires only once.
 * Call once per frame, after gamepad_get_emulator_actions() if needed.
 */
uint32_t gamepad_consume_action_edges(struct gamepad_context *ctx, int player);

#endif /* GAMEPAD_H */
