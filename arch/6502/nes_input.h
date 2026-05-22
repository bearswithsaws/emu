#ifndef __NES_INPUT_H__
#define __NES_INPUT_H__

#include "controller.h"

/**
 * NES Input Handler
 *
 * Provides NES-specific keyboard → controller mapping for use with the
 * generic display library.
 *
 * Player 1 (controller 1):
 * - Arrow keys → D-pad
 * - Z → A, X → B
 * - Enter → Start, Right Shift → Select
 *
 * Player 2 (controller 2):
 * - WASD → D-pad
 * - N → A, M → B
 * - Y → Start, H → Select
 */

/**
 * Initialize NES input system
 *
 * @param ctrl1 - Pointer to player 1 controller structure
 * @param ctrl2 - Pointer to player 2 controller structure
 */
void nes_input_init(struct controller *ctrl1, struct controller *ctrl2);

/**
 * Get the NES input callback
 *
 * Returns the callback function that maps SDL keys to NES controller buttons.
 * Pass this to display_poll_events().
 *
 * @return Input callback function pointer
 */
typedef void (*input_callback_t)(int sdl_keycode, int is_pressed,
                                 void *userdata);
input_callback_t nes_get_input_handler(void);

/**
 * Refresh both controllers from the current SDL keyboard snapshot.
 *
 * The event-driven input handler only updates buttons on KEYDOWN/KEYUP.
 * After a savestate restore (e.g. rewind), previously-set bits can get
 * stuck because SDL never generates a KEYUP for keys it didn't see pressed.
 * Call this once after restoring state to resync with physical key state.
 */
void nes_input_refresh(void);

#endif /* __NES_INPUT_H__ */
