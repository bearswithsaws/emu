#include "controller.h"
#include <string.h>

void controller_init(struct controller *ctrl) {
    memset(ctrl, 0, sizeof(struct controller));
}

void controller_set_button(struct controller *ctrl, uint8_t button, uint8_t pressed) {
    if (pressed) {
        ctrl->buttons |= button;
    } else {
        ctrl->buttons &= ~button;
    }
}

void controller_write(struct controller *ctrl, uint8_t data) {
    ctrl->strobe = data & 0x01;

    // When strobe goes high, latch current button state
    if (ctrl->strobe) {
        ctrl->shift_reg = ctrl->buttons;
    }
}

uint8_t controller_read(struct controller *ctrl) {
    uint8_t result = 0;

    // If strobe is high, continuously reload
    if (ctrl->strobe) {
        result = ctrl->buttons & 0x01;
    } else {
        // Return lowest bit and shift
        result = ctrl->shift_reg & 0x01;
        ctrl->shift_reg >>= 1;
        // Shift in 1s after all 8 buttons read (open bus behavior)
        ctrl->shift_reg |= 0x80;
    }

    return result;
}
