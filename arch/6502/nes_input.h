#ifndef __NES_INPUT_H__
#define __NES_INPUT_H__

#include "controller.h"

/**
 * NES Input Handler
 *
 * Provides NES-specific keyboard → controller mapping for use with the
 * generic display library.
 *
 * Keyboard mapping:
 * - Arrow keys → D-pad
 * - Z → A button
 * - X → B button
 * - Enter → Start
 * - Right Shift → Select
 */

/**
 * Initialize NES input system
 *
 * @param ctrl - Pointer to NES controller structure to update
 */
void nes_input_init(struct controller *ctrl);

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

#endif /* __NES_INPUT_H__ */
