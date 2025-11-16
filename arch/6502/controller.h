#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include <stdint.h>

// NES controller button bits
#define CONTROLLER_A      0x01
#define CONTROLLER_B      0x02
#define CONTROLLER_SELECT 0x04
#define CONTROLLER_START  0x08
#define CONTROLLER_UP     0x10
#define CONTROLLER_DOWN   0x20
#define CONTROLLER_LEFT   0x40
#define CONTROLLER_RIGHT  0x80

struct controller {
    uint8_t buttons;      // Current button state (bitfield)
    uint8_t shift_reg;    // Shift register for serial output
    uint8_t strobe;       // Strobe latch state
};

// Initialize controller
void controller_init(struct controller *ctrl);

// Set button state (1 = pressed, 0 = released)
void controller_set_button(struct controller *ctrl, uint8_t button, uint8_t pressed);

// Write to $4016 (strobe)
void controller_write(struct controller *ctrl, uint8_t data);

// Read from $4016 (serial data)
uint8_t controller_read(struct controller *ctrl);

#endif /* __CONTROLLER_H__ */
