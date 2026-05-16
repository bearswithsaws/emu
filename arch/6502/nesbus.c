// NES Bus

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nesbus.h"
#include "debug.h"

static struct nesbus bus = {0};

// 2 kilobytes ram fir the NES
#define NES_RAM_SIZE (2 * 1024)

static uint8_t ram[NES_RAM_SIZE] = {0};

// Controllers
static struct controller ctrl1 = {0};
static struct controller ctrl2 = {0};

static uint8_t read(uint16_t addr) {
    uint8_t data;

    if (addr < 0x800) {
        data = ram[addr];
    } else if ((addr >= 0x800) && (addr < 0x2000)) {
        // mirroring
        data = ram[addr & 0x7ff];
    } else if ((addr >= 0x2000) && (addr < 0x3fff)) {
        // So wrong but need to implement PPU later
        data = bus.ppu->cpu_read(addr);
    } else if (addr == 0x3fff) {
        // ppu read
    } else if (addr == 0x4016) {
        // Controller 1 read
        data = controller_read(&ctrl1);
        // printf("Controller 1 read: bit=0x%02X (buttons=0x%02X)\n", data,
        // ctrl1.buttons);
    } else if (addr == 0x4017) {
        // Controller 2 read
        data = controller_read(&ctrl2);
    } else if ((addr >= 0x4000) && (addr <= 0x4015)) {
        // APU/other I/O read
        data = 0; // Open bus for now
    } else {
        // remainder of reads like cartridge and open bus?
        data = bus.cart->cpu_read(bus.cart, addr);
    }

    return data;
}

static void write(uint16_t addr, uint8_t data) {
    if (addr < 0x800) {
        ram[addr] = data;
    } else if ((addr >= 0x800) && (addr < 0x2000)) {
        // mirroring
        ram[addr & 0x7ff] = data;
    } else if ((addr >= 0x2000) && (addr < 0x3fff)) {
        // So wrong but need to implement PPU later
        bus.ppu->cpu_write(addr, data);
    } else if (addr == 0x3fff) {
        // ppu write
        LOG_BUS("PPU WRITE\n");
    } else if (addr == 0x4014) {
        uint16_t src_addr = (uint16_t)data << 8;
        for (int i = 0; i < 256; i++) {
            bus.ppu->oam[i] = read(src_addr + i);
        }
        // Stall the CPU for 513 cycles (514 on an odd cycle — approximated as 513).
        bus.dma_halt_cycles = 513;
    } else if (addr == 0x4016) {
        // Controller strobe (writes to both controllers)
        controller_write(&ctrl1, data);
        controller_write(&ctrl2, data);
        // printf("Controller strobe write: 0x%02X (buttons=0x%02X)\n", data,
        // ctrl1.buttons);
    } else if ((addr >= 0x4000) && (addr <= 0x4015)) {
        // APU/other I/O write
        // Ignore for now
    } else if (addr == 0x4017) {
        // APU frame counter
        // Ignore for now
    } else {
        // remainder of reads like cartridge and open bus?
        bus.cart->cpu_write(bus.cart, addr, data);
    }
    return;
}

static void connect_cartridge(struct nes_cartridge *cart) { bus.cart = cart; }

static uint8_t *debug_read(uint16_t offset, uint8_t *buf, uint16_t len) {
    for (int i = 0; i < len; i++) {
        buf[i] = bus.read(offset + i);
    }
    return buf;
}

struct nesbus *nesbus_init(struct cpu6502 *cpu, struct ppu2c02 *ppu) {
    bus.read = read;
    bus.write = write;
    bus.connect_cartridge = connect_cartridge;
    bus.debug_read = debug_read;

    bus.cpu = cpu;
    cpu->connect_bus(&bus);

    bus.ppu = ppu;
    ppu->connect_bus(&bus);

    // Initialize controllers
    controller_init(&ctrl1);
    controller_init(&ctrl2);
    bus.controller1 = &ctrl1;
    bus.controller2 = &ctrl2;

    return &bus;
}