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

    /* Upload the NES framebuffer to the GPU texture. */
    display_upload_framebuffer(display);

    /* Begin ImGui frame. */
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* Full-window dockspace so panels can be docked anywhere. */
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags ds_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##dockspace_root", nullptr, ds_flags);
    ImGui::PopStyleVar(2);
    ImGui::DockSpace(ImGui::GetID("MainDockSpace"),
                     ImVec2(0, 0),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    /* Game viewport window. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Game", nullptr,
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int    gw    = display_get_width(display);
    int    gh    = display_get_height(display);

    /* Scale the 256×240 viewport to fit while preserving aspect ratio. */
    float scale = std::min(avail.x / (float)gw, avail.y / (float)gh);
    ImVec2 img_size(gw * scale, gh * scale);
    ImVec2 cursor((avail.x - img_size.x) * 0.5f,
                  (avail.y - img_size.y) * 0.5f);
    ImGui::SetCursorPos(cursor);
    ImGui::Image((ImTextureID)(intptr_t)game_tex, img_size);
    ImGui::End();
    ImGui::PopStyleVar();

    if (ui && ui->show_demo) {
        ImGui::ShowDemoWindow(&ui->show_demo);
    }

    /* Render ImGui draw data via SDL_Renderer. */
    ImGui::Render();
    SDL_RenderClear(renderer);
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
