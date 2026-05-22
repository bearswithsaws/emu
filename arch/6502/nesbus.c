// NES Bus

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nesbus.h"
#include "2a03.h"
#include "debug.h"

static struct nesbus bus = {0};

static uint8_t ram[NES_RAM_SIZE] = {0};

// Controllers
static struct controller ctrl1 = {0};
static struct controller ctrl2 = {0};

// APU
static struct apu2a03 apu_chip = {0};

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
        data = apu_read(&apu_chip, addr);
    } else {
        if (bus.cart)
            data = bus.cart->cpu_read(bus.cart, addr);
        else
            data = 0xFF;
    }

    if (bus.bp_hook)
        bus.bp_hook(addr, 0, bus.bp_hook_ud);

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
        /* Real hardware: 513 cycles on even CPU cycle, 514 on odd (one extra
         * idle alignment cycle inserted before the 512 read/write cycles). */
        bus.dma_halt_cycles = (bus.total_cycles & 1) ? 514 : 513;
    } else if (addr == 0x4016) {
        // Controller strobe (writes to both controllers)
        controller_write(&ctrl1, data);
        controller_write(&ctrl2, data);
        // printf("Controller strobe write: 0x%02X (buttons=0x%02X)\n", data,
        // ctrl1.buttons);
    } else if ((addr >= 0x4000) && (addr <= 0x4015)) {
        apu_write(&apu_chip, addr, data);
    } else if (addr == 0x4017) {
        apu_write(&apu_chip, addr, data);
    } else {
        if (bus.cart)
            bus.cart->cpu_write(bus.cart, addr, data);
    }

    if (bus.bp_hook)
        bus.bp_hook(addr, 1, bus.bp_hook_ud);

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

    // Initialize APU (pass the static read function for DMC memory reads)
    apu_init(&apu_chip, read);
    bus.apu = &apu_chip;

    return &bus;
}

void nesbus_get_ram(uint8_t *buf) { memcpy(buf, ram, NES_RAM_SIZE); }
void nesbus_set_ram(const uint8_t *buf) { memcpy(ram, buf, NES_RAM_SIZE); }