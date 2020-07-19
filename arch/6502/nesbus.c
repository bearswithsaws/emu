// NES Bus

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nesbus.h"

static struct nesbus bus = {0};

// 2 kilobytes ram fir the NES
#define NES_RAM_SIZE (2 * 1024)

static uint8_t ram[NES_RAM_SIZE] = {0};

static uint8_t read(uint16_t addr) {
    if (addr < 0x800) {
        return ram[addr];
    } else if ((addr >= 0x800) && (addr < 0x2000)) {
        // mirroring
        return ram[addr & 0x7ff];
    } else if ((addr >= 0x2000) && (addr < 0x3fff)) {
        // So wrong but need to implement PPU later
        return bus.ppu->cpu_read(addr);
    } else if (addr == 0x3fff) {
        // ppu read
    } else if ((addr >= 0x4000) && (addr <= 0x4017)) {
        // apu read
        printf("APU READ\n");
    } else {
        // remainder of reads like cartridge and open bus?
        return bus.cart->cpu_read(bus.cart, addr);
    }
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
        printf("PPU WRITE\n");
    } else if ((addr >= 0x4000) && (addr <= 0x4017)) {
        // apu write
        printf("APU WRITE\n");
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

    return &bus;
}