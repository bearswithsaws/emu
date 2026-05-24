#ifndef __NES_INPUT_H__
#define __NES_INPUT_H__

#include <stdint.h>

/* Forward declarations — no SDL2, no controller.h needed here. */
struct nesbus;

/**
 * NES Controller Button Bitmask
 *
 * Standard NES button layout.  Build a bitmask from these constants
 * and pass it to nes_set_buttons() once per frame.
 *
 * Bit  Constant            NES pad button
 * ---  ------------------  --------------
 *  0   NES_BTN_A           A
 *  1   NES_BTN_B           B
 *  2   NES_BTN_SELECT      Select
 *  3   NES_BTN_START       Start
 *  4   NES_BTN_UP          D-pad Up
 *  5   NES_BTN_DOWN        D-pad Down
 *  6   NES_BTN_LEFT        D-pad Left
 *  7   NES_BTN_RIGHT       D-pad Right
 */
#define NES_BTN_A      0x01u
#define NES_BTN_B      0x02u
#define NES_BTN_SELECT 0x04u
#define NES_BTN_START  0x08u
#define NES_BTN_UP     0x10u
#define NES_BTN_DOWN   0x20u
#define NES_BTN_LEFT   0x40u
#define NES_BTN_RIGHT  0x80u

/**
 * Set all held buttons for one controller.
 *
 * The host calls this once per frame (before emu_tick() starts the frame)
 * with the OR of all currently-pressed NES_BTN_* flags.  The emulator reads
 * the bitmask out of the controller shift register each time $4016/$4017 is
 * polled by the game.
 *
 * @param bus     - NES bus (owns controller1 / controller2)
 * @param player  - 1 or 2
 * @param buttons - bitmask of NES_BTN_* flags for all held buttons
 */
void nes_set_buttons(struct nesbus *bus, int player, uint8_t buttons);

#endif /* __NES_INPUT_H__ */
