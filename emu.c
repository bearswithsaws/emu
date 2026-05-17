#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "2c02.h"
#include "6502.h"
#include "2a03.h"
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

    if (argc < 2) {
        fprintf(stderr, "Error: No ROM file specified\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

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

    cartridge = load_rom(argv[1]);
    if (cartridge == NULL) {
        fprintf(stderr, "Error: Failed to load ROM: %s\n", argv[1]);
        display_cleanup(display);
        return EXIT_FAILURE;
    }
    cartridge_info(cartridge);

    cpu = cpu6502_init();
    ppu = ppu2c02_init();
    bus = nesbus_init(cpu, ppu);
    bus->connect_cartridge(cartridge);
    ppu->connect_cartridge(cartridge);
    nes_input_init(bus->controller1, bus->controller2);
    ppu->set_framebuffer(display_get_framebuffer(display));

    /* Open SDL2 audio after the bus (and APU) are initialised so we can
     * calibrate cycles_per_sample to the actual device frequency.
     * SDL_AUDIO_ALLOW_FREQUENCY_CHANGE lets SDL pick the nearest supported
     * rate (common on 48 kHz-native hardware) instead of failing or doing
     * silent software resampling at the wrong ratio. */
    {
        SDL_AudioSpec want = {0}, have = {0};
        want.freq     = 44100;
        want.format   = AUDIO_F32SYS;
        want.channels = 1;
        want.samples  = 512;   /* small device buffer = low hardware latency */
        want.callback = NULL;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_dev == 0) {
            fprintf(stderr, "Warning: SDL audio init failed: %s\n",
                    SDL_GetError());
        } else {
            printf("Audio: %d Hz, device buffer %d samples (%.1f ms)\n",
                   have.freq, have.samples,
                   1000.0 * have.samples / have.freq);
            /* Recalibrate APU downsampler to the frequency SDL actually gave
             * us — fixes pitch/tempo when hardware runs at 48 kHz, not 44.1 */
            bus->apu->cycles_per_sample = (float)(APU_CPU_HZ / (double)have.freq);
            SDL_PauseAudioDevice(audio_dev, 0);
        }
    }

    printf("End of the cartridge:\n");
    bus->debug_read(0xffff - 0xf, buf, 0x10);
    hex_dump(buf, 0x10);

    printf("PC:\n");
    bus->debug_read(cpu->PC, buf, 0x20);
    hex_dump(buf, 0x20);
    cpu->reset();

    /* Boot sequence: run one frame of CPU+APU so the APU frame counter and
     * channel timers are in a sane state before the main loop starts.
     * Discard the generated audio — it is pre-game silence/init noise. */
    printf("Running CPU boot sequence (29780 cycles)...\n");
    for (uint32_t boot_cycle = 0; boot_cycle < 29780; boot_cycle++) {
        cpu->clock();
        apu_clock(bus->apu);
    }
    apu_clear_samples(bus->apu);
    printf("CPU initialization complete.\n");

    printf("Starting emulation loop...\n");
    printf("Controls:\n");
    printf("  ESC=Quit, SPACE=Pause, R=Reset\n");
    printf("  Arrow Keys=D-Pad, Z=A, X=B, Enter=Start, RShift=Select\n");

    while (display_is_running(display)) {
        if (display_poll_events(display, nes_get_input_handler(), NULL)) {
            break;
        }

        if (!display_is_paused(display)) {
            ppu->frame_complete = 0;

            while (!ppu->frame_complete) {
                ppu->clock();
                ppu->clock();
                ppu->clock();

                if (bus->dma_halt_cycles > 0) {
                    bus->dma_halt_cycles--;
                } else {
                    cpu->clock();
                }
                apu_clock(bus->apu);

                if (apu_irq_pending(bus->apu)) {
                    cpu->irq();
                }

                tick_count++;
            }

            /* Flush one frame of audio samples to the SDL queue.
             *
             * Timing strategy: vsync drives frame pacing (primary). The SDL
             * queue is a secondary safety valve — if we are somehow running
             * faster than audio consumption (e.g. vsync not active), we clear
             * the queue rather than letting latency build up. This produces an
             * occasional click but prevents the half-second drift that would
             * accumulate with SDL_Delay-based throttling on platforms where
             * SDL_Delay(1) sleeps 10-15 ms instead of 1 ms. */
            if (audio_dev != 0) {
                float *samples;
                int n = apu_get_samples(bus->apu, &samples);
                if (n > 0) {
                    uint32_t frame_bytes = (uint32_t)(n * sizeof(float));
                    /* Safety valve: >3 frames queued means we're running well
                     * ahead of audio. Clear and restart to kill latency. */
                    if (SDL_GetQueuedAudioSize(audio_dev) > frame_bytes * 3) {
                        SDL_ClearQueuedAudio(audio_dev);
                    }
                    SDL_QueueAudio(audio_dev, samples, frame_bytes);
                    apu_clear_samples(bus->apu);
                }
            }

            frame_count++;

            if (frame_count % 60 == 0) {
                printf("Frame: %u, Ticks: %lu, PC: 0x%04X\n", frame_count,
                       tick_count, cpu->PC);
            }
        }

        display_render_frame(display);
    }

    printf("Emulation stopped. Total frames: %u, Total ticks: %lu\n",
           frame_count, tick_count);

    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    display_cleanup(display);

    // TODO: Add proper cleanup for cartridge, bus, cpu, ppu

    return EXIT_SUCCESS;
}
