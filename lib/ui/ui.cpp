#include "ui.h"

#include <SDL2/SDL.h>
#include <algorithm>

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/backends/imgui_impl_sdl2.h"
#include "vendor/imgui/backends/imgui_impl_sdlrenderer2.h"

extern "C" {
#include "display.h"
}

struct ui_context {
    bool show_demo;
};

/* SDL event hook — forwards every raw SDL_Event to ImGui before display.c
 * processes it. Registered via display_set_event_hook(). */
static void sdl_event_hook(const void *sdl_event, void * /*userdata*/) {
    ImGui_ImplSDL2_ProcessEvent(
        static_cast<const SDL_Event *>(sdl_event));
}

struct ui_context *ui_init(struct display_context *display) {
    SDL_Renderer *renderer =
        static_cast<SDL_Renderer *>(display_get_renderer(display));
    SDL_Window *window =
        static_cast<SDL_Window *>(display_get_window(display));

    if (!renderer || !window) return nullptr;

    ui_context *ui = new ui_context{};
    ui->show_demo  = false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    /* Register the event hook so every SDL_Event is forwarded to ImGui
     * before display_poll_events() consumes it. */
    display_set_event_hook(display, sdl_event_hook, nullptr);

    return ui;
}

void ui_render_frame(struct ui_context *ui, struct display_context *display) {
    SDL_Renderer *renderer =
        static_cast<SDL_Renderer *>(display_get_renderer(display));
    SDL_Texture *game_tex =
        static_cast<SDL_Texture *>(display_get_screen_texture(display));
    int gw = display_get_width(display);
    int gh = display_get_height(display);

    /* Upload the NES framebuffer to the GPU texture. */
    display_upload_framebuffer(display);

    /* Begin ImGui frame — panels/menus declared here overlay the game. */
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (ui && ui->show_demo) {
        ImGui::ShowDemoWindow(&ui->show_demo);
    }

    ImGui::Render();

    /* Render the game texture scaled to fill the window (letterboxed if
     * the window aspect ratio differs from 256:240). */
    SDL_RenderClear(renderer);
    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
    float scale = std::min((float)win_w / gw, (float)win_h / gh);
    SDL_Rect dst = {
        (int)((win_w - gw * scale) * 0.5f),
        (int)((win_h - gh * scale) * 0.5f),
        (int)(gw * scale),
        (int)(gh * scale)
    };
    SDL_RenderCopy(renderer, game_tex, nullptr, &dst);

    /* Draw ImGui on top of the game (menu bar, panels, etc. go here). */
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

void ui_shutdown(struct ui_context *ui) {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    delete ui;
}

void ui_toggle_demo(struct ui_context *ui) {
    if (ui) ui->show_demo = !ui->show_demo;
}
