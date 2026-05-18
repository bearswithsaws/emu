#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "2c02.h"
#include "6502.h"
#include "2a03.h"
#include "cartridge.h"
#include "display.h"
#include "ui.h"
#include "emu_config.h"
#include "nes_input.h"
#include "nesbus.h"

#include "debug.h"
#include "savestate.h"

static struct nesbus *bus;
static struct cpu6502 *cpu;
static struct ppu2c02 *ppu;
static struct ui_context *ui;
static SDL_AudioDeviceID audio_dev = 0;
static struct nes_cartridge *cartridge_global = NULL;
static struct display_context *display_global = NULL;

static void emu_soft_reset(void *userdata) {
    (void)userdata;
    if (cpu && cartridge_global) cpu->reset();
}

static void emu_power_cycle(void *userdata) {
    (void)userdata;
    if (cpu && cartridge_global) cpu->reset();
    /* Full hardware reinit is deferred until save-state support is added. */
}

static void emu_save_state(int slot, void *userdata) {
    (void)userdata;
    if (!cartridge_global) return;
    savestate_save(slot, bus);
}

static void emu_load_state(int slot, void *userdata) {
    (void)userdata;
    if (!cartridge_global) return;
    savestate_load(slot, bus);
}

static void emu_load_rom(const char *path, void *userdata) {
    (void)userdata;
    struct nes_cartridge *new_cart = load_rom(path);
    if (!new_cart) {
        fprintf(stderr, "Failed to load ROM: %s\n", path);
        return;
    }
    /* Swap cartridge — old one is intentionally leaked (no free API yet). */
    cartridge_global = new_cart;
    bus->connect_cartridge(new_cart);
    ppu->connect_cartridge(new_cart);
    cpu->reset();

    /* Update window title to show the loaded filename. */
    const char *slash = strrchr(path, '/');
    const char *name  = slash ? slash + 1 : path;
    char title[256];
    snprintf(title, sizeof(title), "NES Emulator \xe2\x80\x94 %s", name);
    display_set_title(display_global, title);
    display_set_paused(display_global, 0);

    ui_notify_rom_loaded(ui, 1);
    printf("Loaded ROM: %s\n", path);
}

int main(int argc, char *argv[]) {
    struct display_context *display;
    uint8_t buf[0x100];
    uint64_t tick_count = 0;
    uint32_t frame_count = 0;

    printf("NES Emulator version %d.%d\n", emu_VERSION_MAJOR,
           emu_VERSION_MINOR);

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
    display_global = display;

    cpu = cpu6502_init();
    ppu = ppu2c02_init();
    bus = nesbus_init(cpu, ppu);
    nes_input_init(bus->controller1, bus->controller2);
    ppu->set_framebuffer(display_get_framebuffer(display));

    struct ui_callbacks ui_cbs = {
        .on_soft_reset  = emu_soft_reset,
        .on_power_cycle = emu_power_cycle,
        .on_load_rom    = emu_load_rom,
        .on_save_state  = emu_save_state,
        .on_load_state  = emu_load_state,
        .userdata       = NULL,
    };
    ui = ui_init(display, &ui_cbs);

    /* Open SDL2 audio after the bus (and APU) are initialised so we can
     * calibrate cycles_per_sample to the actual device frequency. */
    {
        SDL_AudioSpec want = {0}, have = {0};
        want.freq     = 44100;
        want.format   = AUDIO_F32SYS;
        want.channels = 1;
        want.samples  = 512;
        want.callback = apu_audio_callback;
        want.userdata = bus->apu;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_dev == 0) {
            fprintf(stderr, "Warning: SDL audio init failed: %s\n",
                    SDL_GetError());
        } else {
            printf("Audio: %d Hz, device buffer %d samples (%.1f ms)\n",
                   have.freq, have.samples,
                   1000.0 * have.samples / have.freq);
            bus->apu->cycles_per_sample = (float)(APU_CPU_HZ / (double)have.freq);
            SDL_PauseAudioDevice(audio_dev, 0);
        }
    }

    /* Load initial ROM if one was supplied on the command line. */
    if (argc >= 2) {
        struct nes_cartridge *cartridge = load_rom(argv[1]);
        if (cartridge == NULL) {
            fprintf(stderr, "Warning: Failed to load ROM: %s — starting in idle state\n",
                    argv[1]);
        } else {
            cartridge_global = cartridge;
            cartridge_info(cartridge);
            bus->connect_cartridge(cartridge);
            ppu->connect_cartridge(cartridge);

            printf("End of the cartridge:\n");
            bus->debug_read(0xffff - 0xf, buf, 0x10);
            hex_dump(buf, 0x10);

            printf("PC:\n");
            bus->debug_read(cpu->PC, buf, 0x20);
            hex_dump(buf, 0x20);
            cpu->reset();

            /* Pre-run one frame of CPU+APU so the APU frame counter and channel
             * timers are in a sane state before the main loop starts. */
            printf("Running CPU boot sequence (29780 cycles)...\n");
            for (uint32_t boot_cycle = 0; boot_cycle < 29780; boot_cycle++) {
                cpu->clock();
                apu_clock(bus->apu);
            }
            apu_ring_reset(bus->apu);
            printf("CPU initialization complete.\n");

            ui_notify_rom_loaded(ui, 1);
        }
    }

    printf("Starting emulation loop...\n");
    printf("Controls:\n");
    printf("  ESC=Quit, SPACE=Pause, R=Reset\n");
    printf("  Arrow Keys=D-Pad, Z=A, X=B, Enter=Start, RShift=Select\n");

    while (display_is_running(display)) {
        if (display_poll_events(display, nes_get_input_handler(), NULL)) {
            break;
        }

        /* Tab held = uncapped fast-forward; otherwise use the menu selection. */
        const uint8_t *kb = SDL_GetKeyboardState(NULL);
        float effective_speed = kb[SDL_SCANCODE_TAB]
                                    ? -1.0f
                                    : ui_get_speed_multiplier(ui);

        if (cartridge_global != NULL && !display_is_paused(display)) {
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

            if (audio_dev != 0 && effective_speed > 0.0f &&
                effective_speed <= 1.0f) {
                while (apu_ring_available(bus->apu) > APU_RING_SIZE * 3 / 4) {
                    SDL_Delay(1);
                }
            }
            if (effective_speed > 0.0f && effective_speed < 1.0f) {
                SDL_Delay((uint32_t)(16.0f / effective_speed) - 16u);
            }

            frame_count++;

            if (frame_count % 60 == 0) {
                printf("Frame: %u, Ticks: %llu, PC: 0x%04X\n", frame_count,
                       (unsigned long long)tick_count, cpu->PC);
            }
        } else if (cartridge_global == NULL) {
            /* No ROM loaded — yield the CPU so we don't spin at 100%. */
            SDL_Delay(16);
        }

        ui_render_frame(ui, display);
    }

    printf("Emulation stopped. Total frames: %u, Total ticks: %llu\n",
           frame_count, (unsigned long long)tick_count);

    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    ui_shutdown(ui);
    display_cleanup(display);

    if (cartridge_global)
        cartridge_free(cartridge_global);

    return EXIT_SUCCESS;
}
