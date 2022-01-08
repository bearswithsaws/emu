#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT (WINDOW_WIDTH)

/*
- x, y: upper left corner.
- texture, rect: outputs.
*/
void get_text_and_rect(SDL_Renderer *renderer, int x, int y, char *text,
        TTF_Font *font, SDL_Texture **texture, SDL_Rect *rect) {
    int text_width;
    int text_height;
    SDL_Surface *surface;
    SDL_Color textColor = {255, 255, 255, 0};

    surface = TTF_RenderText_Solid(font, text, textColor);
    *texture = SDL_CreateTextureFromSurface(renderer, surface);
    text_width = surface->w;
    text_height = surface->h;
    SDL_FreeSurface(surface);
    rect->x = x;
    rect->y = y;
    rect->w = text_width;
    rect->h = text_height;
}

void gui_init(void) {
    // SDL_Window *window = NULL;
    // SDL_Renderer *renderer;
    // SDL_Surface *screenSurface = NULL;
    // TTF_Font *font;

    // if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    //     fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
    //     return;
    // }

    // SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    // TTF_Init();

    // SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window,
    //                             &renderer);
    // if (window == NULL) {
    //     fprintf(stderr, "could not create window: %s\n", SDL_GetError());
    //     return;
    // }

    // SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);


    SDL_Event event;
    SDL_Rect rect1, rect2;
    SDL_Renderer *renderer;
    SDL_Texture *texture1, *texture2;
    SDL_Window *window;
    TTF_Font *font;
    int quit;
    int i = 0;
    char buf[100];

    /* Inint TTF. */
    SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer(WINDOW_WIDTH, WINDOW_WIDTH, 0, &window, &renderer);
    TTF_Init();
    font =
        TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 25);

    if (font == NULL) {
        fprintf(stderr, "error: font not found\n");
        exit(EXIT_FAILURE);
    }
    get_text_and_rect(renderer, 0, 0, "Hello", font, &texture1, &rect1);

    quit = 0;
    while (!quit) {
        while (SDL_PollEvent(&event) == 1) {
            if (event.type == SDL_QUIT) {
                quit = 1;
            }
        }
        sprintf( buf, "word %d", i++ );
        get_text_and_rect(renderer, 0, rect1.y + rect1.h, buf, font, &texture2, &rect2);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        /* Use TTF textures. */
        SDL_RenderCopy(renderer, texture1, NULL, &rect1);
        SDL_RenderCopy(renderer, texture2, NULL, &rect2);

        SDL_RenderPresent(renderer);
        SDL_DestroyTexture(texture2);
    }

    /* Deinit TTF. */
    SDL_DestroyTexture(texture1);
    TTF_Quit();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

}