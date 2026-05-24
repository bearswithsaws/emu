#include "input.h"
#include "bus.h"
#include "controller.h"

/**
 * Set all held buttons for one NES controller.
 *
 * The host (e.g. emu.c with SDL2, or any other frontend) translates its
 * input device state into a NES_BTN_* bitmask and calls this once per
 * frame.  lib_nes itself has no knowledge of SDL2, keyboards, or gamepads.
 */
void nes_set_buttons(struct nesbus *bus, int player, uint8_t buttons) {
    if (!bus) return;
    if (player == 1 && bus->controller1)
        bus->controller1->buttons = buttons;
    else if (player == 2 && bus->controller2)
        bus->controller2->buttons = buttons;
}
