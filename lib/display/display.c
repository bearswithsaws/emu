#include "display.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Internal display context structure
struct display_context {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screen_texture;
    uint32_t *frame_buffer;

    // Configuration
    int screen_width;
    int screen_height;
    int scale_factor;

    // State
    int running;
    int paused;
};

struct display_context *display_init(const struct display_config *config) {
    struct display_context *ctx;

    if (!config) {
        fprintf(stderr, "Error: NULL display config\n");
        return NULL;
    }

    ctx = (struct display_context *)malloc(sizeof(struct display_context));
    if (!ctx) {
        fprintf(stderr, "Error: Failed to allocate display context\n");
        return NULL;
    }

    memset(ctx, 0, sizeof(struct display_context));
    ctx->screen_width = config->screen_width;
    ctx->screen_height = config->screen_height;
    ctx->scale_factor = config->scale_factor;
    ctx->running = 1;
    ctx->paused = 0;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "Error: Failed to initialize SDL: %s\n",
                SDL_GetError());
        free(ctx);
        return NULL;
    }

    // Create window
    ctx->window = SDL_CreateWindow(
        config->window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        config->screen_width * config->scale_factor,
        config->screen_height * config->scale_factor, SDL_WINDOW_SHOWN);

    if (!ctx->window) {
        fprintf(stderr, "Error: Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        free(ctx);
        return NULL;
    }

    // Create renderer with optional vsync
    uint32_t renderer_flags = SDL_RENDERER_ACCELERATED;
    if (config->enable_vsync) {
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, renderer_flags);

    if (!ctx->renderer) {
        fprintf(stderr, "Error: Failed to create renderer: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        free(ctx);
        return NULL;
    }

    // Use linear filtering for scaling
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    // Create texture for screen
    ctx->screen_texture = SDL_CreateTexture(
        ctx->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        config->screen_width, config->screen_height);

    if (!ctx->screen_texture) {
        fprintf(stderr, "Error: Failed to create screen texture: %s\n",
                SDL_GetError());
        SDL_DestroyRenderer(ctx->renderer);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        free(ctx);
        return NULL;
    }

    // Allocate frame buffer
    ctx->frame_buffer = (uint32_t *)calloc(
        config->screen_width * config->screen_height, sizeof(uint32_t));

    if (!ctx->frame_buffer) {
        fprintf(stderr, "Error: Failed to allocate frame buffer\n");
        SDL_DestroyTexture(ctx->screen_texture);
        SDL_DestroyRenderer(ctx->renderer);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        free(ctx);
        return NULL;
    }

    // Initialize to a gray checkerboard pattern
    for (int y = 0; y < config->screen_height; y++) {
        for (int x = 0; x < config->screen_width; x++) {
            int checker = ((x / 8) + (y / 8)) % 2;
            ctx->frame_buffer[y * config->screen_width + x] =
                checker ? 0xFF404040 : 0xFF606060;
        }
    }

    printf("Display initialized: %dx%d window (scale %dx), vsync %s\n",
           config->screen_width * config->scale_factor,
           config->screen_height * config->scale_factor, config->scale_factor,
           config->enable_vsync ? "on" : "off");

    return ctx;
}

void display_cleanup(struct display_context *ctx) {
    if (!ctx)
        return;

    if (ctx->frame_buffer) {
        free(ctx->frame_buffer);
    }

    if (ctx->screen_texture) {
        SDL_DestroyTexture(ctx->screen_texture);
    }

    if (ctx->renderer) {
        SDL_DestroyRenderer(ctx->renderer);
    }

    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
    }

    SDL_Quit();
    free(ctx);

    printf("Display cleaned up\n");
}

uint32_t *display_get_framebuffer(struct display_context *ctx) {
    return ctx ? ctx->frame_buffer : NULL;
}

int display_poll_events(struct display_context *ctx,
                        input_callback_t input_handler, void *userdata) {
    SDL_Event event;

    if (!ctx)
        return 1;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            ctx->running = 0;
            return 1; // Signal to quit

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                ctx->running = 0;
                return 1; // Signal to quit

            case SDLK_SPACE:
                ctx->paused = !ctx->paused;
                printf("Emulation %s\n", ctx->paused ? "paused" : "resumed");
                break;

            case SDLK_r:
                printf("Reset requested\n");
                // TODO: Could add reset callback in future
                break;

            default:
                // Pass other keys to input handler
                if (input_handler) {
                    input_handler(event.key.keysym.sym, 1, userdata);
                }
                break;
            }
            break;

        case SDL_KEYUP:
            // Pass key releases to input handler
            if (input_handler) {
                input_handler(event.key.keysym.sym, 0, userdata);
            }
            break;

        default:
            break;
        }
    }

    return 0; // Continue running
}

void display_render_frame(struct display_context *ctx) {
    if (!ctx || !ctx->renderer || !ctx->screen_texture || !ctx->frame_buffer) {
        return;
    }

    // Update texture with frame buffer data
    SDL_UpdateTexture(
        ctx->screen_texture,
        NULL, // Update entire texture
        ctx->frame_buffer,
        ctx->screen_width * sizeof(uint32_t) // Pitch (bytes per row)
    );

    // Clear renderer
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    // Render the screen texture (scaled)
    SDL_RenderCopy(ctx->renderer, ctx->screen_texture, NULL, NULL);

    // Present to screen
    SDL_RenderPresent(ctx->renderer);
}

int display_is_running(struct display_context *ctx) {
    return ctx ? ctx->running : 0;
}

int display_is_paused(struct display_context *ctx) {
    return ctx ? ctx->paused : 0;
}

void display_set_paused(struct display_context *ctx, int paused) {
    if (ctx) {
        ctx->paused = paused;
    }
}
