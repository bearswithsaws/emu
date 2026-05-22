#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

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
#include "sram.h"
#include "disasm.h"
#include "rewind.h"
#include "tas.h"
#include "gamepad.h"

static struct nesbus *bus;
static struct cpu6502 *cpu;
static struct ppu2c02 *ppu;
static struct ui_context *ui;
static SDL_AudioDeviceID audio_dev = 0;
static struct nes_cartridge *cartridge_global = NULL;
static struct display_context *display_global = NULL;
static uint64_t tick_count_global = 0;
static char rom_path_global[1024] = {0};  /* path of the currently-loaded ROM */
static struct rewind_buffer *rewind_buf = NULL;
static struct tas_context   *tas_global = NULL;
static struct gamepad_context *gamepad_global = NULL;

/* Run one NES clock tick: 3 PPU cycles + 1 CPU cycle + 1 APU cycle. */
static void emu_tick(void) {
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
    /* Per-CPU-cycle mapper clock (e.g. Sunsoft FME-7 IRQ counter). */
    if (bus->cart && bus->cart->map && bus->cart->map->clock)
        bus->cart->map->clock(bus->cart->map);
    tick_count_global++;
    bus->total_cycles++;
}

/* Step exactly one CPU instruction (finishes any partial instruction first). */
static void emu_step_one(void) {
    /* Complete any partially-executed instruction (multi-cycle). */
    while (cpu->cycles > 0) emu_tick();
    /* Fetch and execute exactly one instruction. */
    do { emu_tick(); } while (cpu->cycles > 0);
}

/* Step over: run instructions until PC reaches target. */
static void emu_step_over(uint16_t target) {
    for (int guard = 0; guard < 100000; guard++) {
        emu_step_one();
        if (cpu->PC == target) break;
    }
}

static void emu_soft_reset(void *userdata) {
    (void)userdata;
    if (cpu && cartridge_global) {
        cpu->reset();
        apu_reset(bus->apu);
    }
}

static void emu_power_cycle(void *userdata) {
    (void)userdata;
    if (cpu && cartridge_global) {
        cpu->reset();
        apu_reset(bus->apu);
    }
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

static void emu_screenshot(void *userdata) {
    (void)userdata;
    uint32_t *fb = display_get_framebuffer(display_global);
    if (!fb) return;

    /* Build ~/.local/share/emu/screenshots/ path. */
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home) return;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.local/share/emu/screenshots", home);
    /* Create directory hierarchy (ignore errors if it already exists). */
    {
        char tmp[512];
        const char *segs[] = { "/.local", "/.local/share", "/.local/share/emu",
                                "/.local/share/emu/screenshots" };
        for (int i = 0; i < 4; i++) {
            snprintf(tmp, sizeof(tmp), "%s%s", home, segs[i]);
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
        }
    }

    /* Timestamped filename: emu_YYYYMMDD_HHMMSS.bmp */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char path[600];
    snprintf(path, sizeof(path),
             "%s/emu_%04d%02d%02d_%02d%02d%02d.bmp",
             dir,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    /* Wrap the ARGB8888 framebuffer in an SDL_Surface and save as BMP. */
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
        fb, 256, 240, 32, 256 * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        fprintf(stderr, "screenshot: SDL_CreateRGBSurface failed: %s\n",
                SDL_GetError());
        return;
    }
    if (SDL_SaveBMP(surf, path) != 0)
        fprintf(stderr, "screenshot: SDL_SaveBMP failed: %s\n", SDL_GetError());
    else
        printf("Screenshot saved: %s\n", path);
    SDL_FreeSurface(surf);
}

static void emu_tas_record_start(const char *path, void *userdata) {
    (void)userdata;
    if (!tas_global || !cartridge_global) return;
    if (tas_start_record(tas_global, path) == 0)
        printf("TAS: recording to '%s'\n", path);
}

static void emu_tas_stop(void *userdata) {
    (void)userdata;
    if (!tas_global) return;
    tas_stop(tas_global);
    printf("TAS: stopped\n");
}

static void emu_tas_play(const char *path, void *userdata) {
    (void)userdata;
    if (!tas_global || !cartridge_global) return;
    if (tas_start_play(tas_global, path) == 0)
        printf("TAS: playing from '%s'\n", path);
}

static void emu_load_rom(const char *path, void *userdata) {
    (void)userdata;
    struct nes_cartridge *new_cart = load_rom(path);
    if (!new_cart) {
        fprintf(stderr, "Failed to load ROM: %s\n", path);
        return;
    }
    /* Save SRAM for the outgoing ROM before swapping. */
    if (cartridge_global && rom_path_global[0])
        sram_save(cartridge_global, rom_path_global);

    /* Swap cartridge — old one is intentionally leaked (no free API yet). */
    cartridge_global = new_cart;
    snprintf(rom_path_global, sizeof(rom_path_global), "%s", path);
    bus->connect_cartridge(new_cart);
    ppu->connect_cartridge(new_cart);
    cpu->reset();
    apu_reset(bus->apu);

    /* Load battery-backed SRAM if the cartridge supports it. */
    sram_load(new_cart, path);

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
    rewind_buf = rewind_init();
    tas_global = tas_init();
    gamepad_global = gamepad_init();
    ppu->set_framebuffer(display_get_framebuffer(display));

    struct ui_callbacks ui_cbs = {
        .on_soft_reset  = emu_soft_reset,
        .on_power_cycle = emu_power_cycle,
        .on_load_rom    = emu_load_rom,
        .on_save_state  = emu_save_state,
        .on_load_state  = emu_load_state,
        .on_screenshot  = emu_screenshot,
        .on_tas_record  = emu_tas_record_start,
        .on_tas_stop    = emu_tas_stop,
        .on_tas_play    = emu_tas_play,
        .userdata       = NULL,
    };
    ui = ui_init(display, &ui_cbs);
    ui_set_debug_context(ui, cpu, ppu, bus);

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
            snprintf(rom_path_global, sizeof(rom_path_global), "%s", argv[1]);
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
            apu_reset(bus->apu);

            /* Pre-run one frame of CPU+APU so the APU frame counter and channel
             * timers are in a sane state before the main loop starts. */
            printf("Running CPU boot sequence (29780 cycles)...\n");
            for (uint32_t boot_cycle = 0; boot_cycle < 29780; boot_cycle++) {
                cpu->clock();
                apu_clock(bus->apu);
            }
            apu_ring_reset(bus->apu);
            printf("CPU initialization complete.\n");

            /* Load battery-backed SRAM if the cartridge supports it. */
            sram_load(cartridge, argv[1]);

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

        /* Refresh gamepad state (hot-plug + poll current buttons). */
        gamepad_refresh(gamepad_global);

        /* Tab held = uncapped fast-forward; otherwise use the menu selection. */
        const uint8_t *kb = SDL_GetKeyboardState(NULL);
        float effective_speed = kb[SDL_SCANCODE_TAB]
                                    ? -1.0f
                                    : ui_get_speed_multiplier(ui);

        if (cartridge_global != NULL) {
            /* --- Debugger step controls (processed while paused) --- */
            uint16_t step_over_target;
            if (ui_debugger_consume_step(ui)) {
                emu_step_one();
                display_set_paused(display, 1);
            } else if (ui_debugger_consume_step_over(ui, &step_over_target)) {
                emu_step_over(step_over_target);
                display_set_paused(display, 1);
            }

            /* --- Rewind or normal frame emulation (only when running) --- */
            if (!display_is_paused(display)) {
                int rewinding = ui_get_rewind_held(ui);

                if (rewinding) {
                    /* Mute audio while rewinding to avoid noise. */
                    if (audio_dev != 0) SDL_PauseAudioDevice(audio_dev, 1);

                    if (!rewind_step(rewind_buf, bus)) {
                        /* No more history — stay on the oldest frame. */
                    }

                    /* Resync controller state from actual keyboard so bits
                     * restored by the savestate don't get stuck. */
                    nes_input_refresh();
                    bus->controller1->buttons |= gamepad_get_buttons(gamepad_global, 0);
                    bus->controller2->buttons |= gamepad_get_buttons(gamepad_global, 1);

                    /* Render one PPU frame so the display reflects the
                     * restored state — without this the screen freezes. */
                    ppu->frame_complete = 0;
                    while (!ppu->frame_complete)
                        emu_tick();
                } else {
                    /* Capture a snapshot before running this frame so that
                     * rewinding can restore it. */
                    rewind_push(rewind_buf, bus);

                    /* Unmute audio when not rewinding. */
                    if (audio_dev != 0) SDL_PauseAudioDevice(audio_dev, 0);

                    /* TAS playback overrides all input; otherwise merge gamepad. */
                    if (tas_get_mode(tas_global) == TAS_PLAYING) {
                        uint8_t tas_p1 = 0, tas_p2 = 0;
                        tas_get_buttons(tas_global, &tas_p1, &tas_p2);
                        bus->controller1->buttons = tas_p1;
                        bus->controller2->buttons = tas_p2;
                    } else {
                        bus->controller1->buttons |= gamepad_get_buttons(gamepad_global, 0);
                        bus->controller2->buttons |= gamepad_get_buttons(gamepad_global, 1);
                    }

                    ppu->frame_complete = 0;

                    while (!ppu->frame_complete) {
                        emu_tick();

                        /* Breakpoint check: pause on execute or read/write breakpoint hit.
                         * Break immediately so the debugger shows the exact PC at the hit,
                         * not wherever the CPU ends up at the end of the frame. */
                        if (ui_debugger_is_breakpoint(ui, cpu->PC) ||
                            ui_debugger_consume_rw_bp_hit(ui)) {
                            display_set_paused(display, 1);
                            break;
                        }
                    }

                    /* TAS tick: record or advance playback after each frame. */
                    if (tas_global) {
                        uint8_t p1 = bus->controller1->buttons;
                        uint8_t p2 = bus->controller2->buttons;
                        int done = tas_tick(tas_global, p1, p2);
                        if (done) {
                            /* Playback finished — tas_tick() already called tas_stop(). */
                            printf("TAS: playback complete at frame %u\n",
                                   tas_get_frame(tas_global));
                        }
                    }

                    /* Update the UI with current TAS state. */
                    ui_set_tas_state(ui, (int)tas_get_mode(tas_global),
                                     tas_get_frame(tas_global));

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
                               (unsigned long long)tick_count_global, cpu->PC);
                    }
                }
            }
        } else if (cartridge_global == NULL) {
            /* No ROM loaded — yield the CPU so we don't spin at 100%. */
            SDL_Delay(16);
        }

        ui_debugger_update_cycles(ui, tick_count_global);
        ui_render_frame(ui, display);
    }

    printf("Emulation stopped. Total frames: %u, Total ticks: %llu\n",
           frame_count, (unsigned long long)tick_count_global);

    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    rewind_free(rewind_buf);
    tas_free(tas_global);
    gamepad_free(gamepad_global);
    ui_shutdown(ui);
    display_cleanup(display);

    if (cartridge_global) {
        sram_save(cartridge_global, rom_path_global);
        cartridge_free(cartridge_global);
    }

    return EXIT_SUCCESS;
}
