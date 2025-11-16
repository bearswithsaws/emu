#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "2c02.h"
#include "6502.h"
#include "cartridge.h"
#include "display.h"
#include "emu_config.h"
#include "nes_input.h"
#include "nesbus.h"

#include "debug.h"

static struct nesbus *bus;
static struct cpu6502 *cpu;
static struct ppu2c02 *ppu;

static void print_usage(const char *prog_name) {
    printf("Usage: %s <rom_file.nes>\n", prog_name);
    printf("\nNES Emulator - Version %d.%d\n", emu_VERSION_MAJOR,
           emu_VERSION_MINOR);
    printf("\nArguments:\n");
    printf("  <rom_file.nes>    Path to NES ROM file (iNES format)\n");
    printf("\nExamples:\n");
    printf("  %s mario.nes\n", prog_name);
    printf("  %s /path/to/rom/game.nes\n", prog_name);
}

int main(int argc, char *argv[]) {
    struct nes_cartridge *cartridge;
    struct display_context *display;
    uint8_t buf[0x100];
    uint64_t tick_count = 0;
    uint32_t frame_count = 0;

    printf("NES Emulator version %d.%d\n", emu_VERSION_MAJOR,
           emu_VERSION_MINOR);

    // Check for ROM file argument
    if (argc < 2) {
        fprintf(stderr, "Error: No ROM file specified\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Initialize display with NES configuration
    struct display_config config = {.window_title = "NES Emulator",
                                    .screen_width = 256,
                                    .screen_height = 240,
                                    .scale_factor = 3,
                                    .enable_vsync = 1};

    display = display_init(&config);
    if (!display) {
        fprintf(stderr, "Error: Failed to initialize display\n");
        return EXIT_FAILURE;
    }

    // Load the ROM
    cartridge = load_rom(argv[1]);
    if (cartridge == NULL) {
        fprintf(stderr, "Error: Failed to load ROM: %s\n", argv[1]);
        display_cleanup(display);
        return EXIT_FAILURE;
    }
    cartridge_info(cartridge);

    // Initialize emulator components
    cpu = cpu6502_init();
    ppu = ppu2c02_init();
    bus = nesbus_init(cpu, ppu);
    bus->connect_cartridge(cartridge);

    // Connect cartridge to PPU for CHR-ROM access
    ppu->connect_cartridge(cartridge);

    // Initialize NES input handler
    nes_input_init(bus->controller1);

    // Connect PPU to display frame buffer for rendering
    ppu->set_framebuffer(display_get_framebuffer(display));

    printf("End of the cartridge:\n");
    bus->debug_read(0xffff - 0xf, buf, 0x10);
    hex_dump(buf, 0x10);

    printf("PC:\n");
    bus->debug_read(cpu->PC, buf, 0x20);
    hex_dump(buf, 0x20);
    cpu->reset();

    // Run CPU initialization sequence before starting PPU rendering
    // NES games expect ~1 frame (29780 CPU cycles) to initialize memory
    // This allows games to clear nametables, load palettes, and set up PPU
    // registers
    printf("Running CPU boot sequence (29780 cycles)...\n");
    for (uint32_t boot_cycle = 0; boot_cycle < 29780; boot_cycle++) {
        cpu->clock();
    }
    printf("CPU initialization complete.\n");

    printf("Starting emulation loop...\n");
    printf("Controls:\n");
    printf("  ESC=Quit, SPACE=Pause, R=Reset\n");
    printf("  Arrow Keys=D-Pad, Z=A, X=B, Enter=Start, RShift=Select\n");

    // Main emulation loop - integrated with display
    while (display_is_running(display)) {
        // Handle display events (keyboard, window close, etc.)
        // Input callback updates controller state directly
        if (display_poll_events(display, nes_get_input_handler(), NULL)) {
            break; // User wants to quit
        }

        // Run emulation for one frame (if not paused)
        if (!display_is_paused(display)) {
            // NMI is now implemented, so games can enable rendering themselves

            // Reset frame complete flag
            ppu->frame_complete = 0;

            // Run until PPU completes a frame (ends at scanline 241, dot 1)
            while (!ppu->frame_complete) {
                // PPU clock (3x per CPU clock)
                ppu->clock();
                ppu->clock();
                ppu->clock();

                // CPU clock
                cpu->clock();

                // Temporary: Write random value for nestest compatibility
                // TODO: Remove this when proper controller input is implemented
                // cpu->write(0xd2, (uint8_t)(tick_count & 0xff));

                tick_count++;
            }

            frame_count++;

            // Debug output every 60 frames (1 second at 60fps)
            if (frame_count % 60 == 0) {
                printf("Frame: %u, Ticks: %lu, PC: 0x%04X\n", frame_count,
                       tick_count, cpu->PC);
            }
        }

        // Render the completed frame (PPU has written to frame_buffer)
        display_render_frame(display);
    }

    printf("Emulation stopped. Total frames: %u, Total ticks: %lu\n",
           frame_count, tick_count);

    // Cleanup
    display_cleanup(display);

    // TODO: Add proper cleanup for cartridge, bus, cpu, ppu
    // (Currently they are static globals that will be freed on program exit)

    return EXIT_SUCCESS;
}