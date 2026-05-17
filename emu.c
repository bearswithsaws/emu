#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "2c02.h"
#include "6502.h"
#include "apu.h"
#include "cartridge.h"
#include "display.h"
#include "emu_config.h"
#include "nes_input.h"
#include "nesbus.h"

#include "debug.h"

static struct nesbus *bus;
static struct cpu6502 *cpu;
static struct ppu2c02 *ppu;
static SDL_AudioDeviceID audio_dev = 0;

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

    /* Open SDL2 audio device in queue mode */
    {
        SDL_AudioSpec want = {0}, have = {0};
        want.freq     = 44100;
        want.format   = AUDIO_F32SYS;
        want.channels = 1;
        want.samples  = 1024;
        want.callback = NULL;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio_dev == 0) {
            fprintf(stderr, "Warning: SDL audio init failed: %s\n", SDL_GetError());
        } else {
            SDL_PauseAudioDevice(audio_dev, 0);
        }
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
    nes_input_init(bus->controller1, bus->controller2);

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

                // APU clock (once per CPU cycle)
                apu_clock(bus->apu);

                // Check APU IRQ
                if (apu_irq_pending(bus->apu)) {
                    cpu->irq();
                }

                tick_count++;
            }

            /* Flush audio samples to SDL */
            if (audio_dev != 0) {
                float *samples;
                int n = apu_get_samples(bus->apu, &samples);
                if (n > 0) {
                    /* Throttle: if queue has more than 2 frames buffered, wait */
                    uint32_t queued;
                    while ((queued = SDL_GetQueuedAudioSize(audio_dev)) >
                           (uint32_t)(2 * n * (int)sizeof(float))) {
                        SDL_Delay(1);
                    }
                    SDL_QueueAudio(audio_dev, samples, (uint32_t)(n * sizeof(float)));
                    apu_clear_samples(bus->apu);
                }
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
    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    display_cleanup(display);

    // TODO: Add proper cleanup for cartridge, bus, cpu, ppu
    // (Currently they are static globals that will be freed on program exit)

    return EXIT_SUCCESS;
}