#include "ui.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/backends/imgui_impl_sdl2.h"
#include "vendor/imgui/backends/imgui_impl_sdlrenderer2.h"
#include <nfd.h>

extern "C" {
#include "display.h"
#include "emu_config.h"
}

#define MAX_RECENT_ROMS 5

struct ui_context {
    bool show_demo;
    bool show_menubar;
    bool show_fps;

    /* Debug panel visibility */
    bool show_cpu_debugger;
    bool show_memory_viewer;
    bool show_ppu_viewer;
    bool show_apu_visualizer;

    /* Speed: 0.5 / 1.0 / 2.0 / -1.0 (uncapped) */
    float speed_multiplier;

    /* Open ROM file dialog request (processed after frame is presented) */
    bool open_file_dialog_requested;

    /* Controls / About popups */
    bool controls_popup_requested;
    bool about_popup_requested;

    /* Recent ROMs (most-recent first, max MAX_RECENT_ROMS) */
    std::vector<std::string> recent_roms;

    /* Emulator callbacks */
    ui_callbacks callbacks;

    /* FPS tracking */
    float fps;
    Uint32 fps_last_tick;
    int fps_frame_count;

    /* Splash / idle state */
    bool rom_loaded;
    char pending_drop_path[1024];
};

/* ---------- SDL event hook ------------------------------------------------ */

static void sdl_event_hook(const void *sdl_event, void *userdata) {
    const SDL_Event *e = static_cast<const SDL_Event *>(sdl_event);
    ImGui_ImplSDL2_ProcessEvent(e);

    if (e->type == SDL_DROPFILE && e->drop.file && userdata) {
        ui_context *ui = static_cast<ui_context *>(userdata);
        strncpy(ui->pending_drop_path, e->drop.file,
                sizeof(ui->pending_drop_path) - 1);
        ui->pending_drop_path[sizeof(ui->pending_drop_path) - 1] = '\0';
    }
}

/* ---------- Recent ROMs persistence --------------------------------------- */

static std::string recent_roms_path() {
    const char *home = SDL_getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.config/emu/recent.txt";
}

static void create_directory(const char *path) {
#ifdef _WIN32
    mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void ensure_config_dir() {
    const char *home = SDL_getenv("HOME");
    if (!home) return;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config", home);
    create_directory(dir);
    snprintf(dir, sizeof(dir), "%s/.config/emu", home);
    create_directory(dir);
}

static void load_recent_roms(ui_context *ui) {
    ui->recent_roms.clear();
    std::ifstream f(recent_roms_path());
    std::string line;
    while (std::getline(f, line) &&
           (int)ui->recent_roms.size() < MAX_RECENT_ROMS) {
        if (!line.empty())
            ui->recent_roms.push_back(line);
    }
}

static void save_recent_roms(ui_context *ui) {
    ensure_config_dir();
    std::ofstream f(recent_roms_path());
    for (const auto &r : ui->recent_roms)
        f << r << "\n";
}

static void add_recent_rom(ui_context *ui, const char *path) {
    std::string p(path);
    ui->recent_roms.erase(
        std::remove(ui->recent_roms.begin(), ui->recent_roms.end(), p),
        ui->recent_roms.end());
    ui->recent_roms.insert(ui->recent_roms.begin(), p);
    if ((int)ui->recent_roms.size() > MAX_RECENT_ROMS)
        ui->recent_roms.resize(MAX_RECENT_ROMS);
    save_recent_roms(ui);
}

/* ---------- ROM loading helper -------------------------------------------- */

static void do_load_rom(ui_context *ui, const char *path) {
    if (!path || path[0] == '\0') return;
    if (ui->callbacks.on_load_rom)
        ui->callbacks.on_load_rom(path, ui->callbacks.userdata);
    add_recent_rom(ui, path);
    ui->rom_loaded = true;
}

/* ---------- Menu rendering helpers ---------------------------------------- */

static void render_menu_file(ui_context *ui, display_context *display) {
    if (!ImGui::BeginMenu("File")) return;

    if (ImGui::MenuItem("Open ROM\xe2\x80\xa6", "Ctrl+O")) {
        ui->open_file_dialog_requested = true;
    }

    /* Recent ROMs sub-menu */
    bool has_recent = !ui->recent_roms.empty();
    if (ImGui::BeginMenu("Recent ROMs", has_recent)) {
        for (const auto &rom : ui->recent_roms) {
            /* Show only the filename portion for readability */
            const char *label = rom.c_str();
            const char *slash = strrchr(label, '/');
            if (slash) label = slash + 1;
            if (ImGui::MenuItem(label)) {
                do_load_rom(ui, rom.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", rom.c_str());
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Close ROM")) {
        display_set_paused(display, 1);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Quit", "Esc")) {
        display_request_quit(display);
    }

    ImGui::EndMenu();
}

static void render_menu_emulation(ui_context *ui, display_context *display) {
    if (!ImGui::BeginMenu("Emulation")) return;

    bool paused = display_is_paused(display) != 0;
    if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Space")) {
        display_set_paused(display, paused ? 0 : 1);
    }

    if (ImGui::MenuItem("Reset (Soft)", "R")) {
        if (ui->callbacks.on_soft_reset)
            ui->callbacks.on_soft_reset(ui->callbacks.userdata);
    }

    if (ImGui::MenuItem("Power Cycle (Hard)")) {
        if (ui->callbacks.on_power_cycle)
            ui->callbacks.on_power_cycle(ui->callbacks.userdata);
    }

    ImGui::Separator();

    /* Save/Load state slots — not yet implemented */
    ImGui::BeginDisabled(true);
    if (ImGui::BeginMenu("Save State")) {
        for (int i = 1; i <= 5; i++) {
            char label[16];
            snprintf(label, sizeof(label), "Slot %d", i);
            ImGui::MenuItem(label);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Load State")) {
        for (int i = 1; i <= 5; i++) {
            char label[16];
            snprintf(label, sizeof(label), "Slot %d", i);
            ImGui::MenuItem(label);
        }
        ImGui::EndMenu();
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::BeginMenu("Speed")) {
        if (ImGui::MenuItem("50%",      nullptr, ui->speed_multiplier == 0.5f))
            ui->speed_multiplier = 0.5f;
        if (ImGui::MenuItem("100%",     nullptr, ui->speed_multiplier == 1.0f))
            ui->speed_multiplier = 1.0f;
        if (ImGui::MenuItem("200%",     nullptr, ui->speed_multiplier == 2.0f))
            ui->speed_multiplier = 2.0f;
        if (ImGui::MenuItem("Uncapped", nullptr, ui->speed_multiplier == -1.0f))
            ui->speed_multiplier = -1.0f;
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

static void render_menu_debug(ui_context *ui) {
    if (!ImGui::BeginMenu("Debug")) return;

    ImGui::MenuItem("CPU Debugger",    nullptr, &ui->show_cpu_debugger);
    ImGui::MenuItem("Memory Viewer",   nullptr, &ui->show_memory_viewer);
    ImGui::MenuItem("PPU Viewer",      nullptr, &ui->show_ppu_viewer);
    ImGui::MenuItem("APU Visualizer",  nullptr, &ui->show_apu_visualizer);

    ImGui::EndMenu();
}

static void render_menu_view(ui_context *ui, display_context *display) {
    if (!ImGui::BeginMenu("View")) return;

    int cur_scale = display_get_scale(display);
    for (int s = 1; s <= 4; s++) {
        char label[8];
        snprintf(label, sizeof(label), "%d\xc3\x97", s);  /* "Nx" */
        if (ImGui::MenuItem(label, nullptr, cur_scale == s))
            display_set_scale(display, s);
    }

    ImGui::Separator();

    bool fs = display_is_fullscreen(display) != 0;
    if (ImGui::MenuItem("Fullscreen", "F11", fs))
        display_toggle_fullscreen(display);

    ImGui::Separator();

    ImGui::MenuItem("Show FPS Overlay", nullptr, &ui->show_fps);

    if (ImGui::MenuItem("Toggle Menubar", "F1"))
        ui->show_menubar = !ui->show_menubar;

    ImGui::EndMenu();
}

static void render_menu_help(ui_context *ui) {
    if (!ImGui::BeginMenu("Help")) return;

    if (ImGui::MenuItem("Controls Reference\xe2\x80\xa6"))
        ui->controls_popup_requested = true;

    if (ImGui::MenuItem("About\xe2\x80\xa6"))
        ui->about_popup_requested = true;

    ImGui::EndMenu();
}

/* ---------- Modal popups -------------------------------------------------- */

static void render_controls_popup() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Controls Reference##modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("Controller");
        ImGui::Text("D-Pad       Arrow Keys");
        ImGui::Text("A           Z");
        ImGui::Text("B           X");
        ImGui::Text("Start       Enter");
        ImGui::Text("Select      Right Shift");
        ImGui::SeparatorText("Emulator");
        ImGui::Text("Pause       Space");
        ImGui::Text("Reset       R");
        ImGui::Text("Quit        Escape");
        ImGui::SeparatorText("UI");
        ImGui::Text("Menubar     F1");
        ImGui::Text("Fullscreen  F11");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void render_about_popup() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("About##modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("NES Emulator  v%d.%d",
                    emu_VERSION_MAJOR, emu_VERSION_MINOR);
        ImGui::Separator();
        ImGui::Text("CPU:     MOS 6502 / 2A03");
        ImGui::Text("PPU:     Ricoh 2C02");
        ImGui::Text("APU:     2A03 (all 5 channels)");
        ImGui::Text("Mappers: 0, 1, 2, 3, 4, 7, 11, 66");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

/* ---------- No-ROM splash ------------------------------------------------- */

static void render_no_rom_splash(ui_context *ui, display_context *display,
                                  float menu_height) {
    SDL_Renderer *renderer =
        static_cast<SDL_Renderer *>(display_get_renderer(display));
    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
    float avail_h = (float)win_h - menu_height;

    const float panel_w = 340.0f;
    const float panel_h = 200.0f;
    ImGui::SetNextWindowPos(
        ImVec2(win_w * 0.5f, menu_height + avail_h * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove       |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("##splash", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 24.0f);

    auto center_text = [&](const char *text) {
        float tw = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((panel_w - tw) * 0.5f);
        ImGui::TextUnformatted(text);
    };

    center_text("Drop a .nes file here");
    ImGui::Spacing();
    {
        float tw = ImGui::CalcTextSize("\xe2\x80\x94 or \xe2\x80\x94").x;
        ImGui::SetCursorPosX((panel_w - tw) * 0.5f);
        ImGui::TextDisabled("\xe2\x80\x94 or \xe2\x80\x94");
    }
    ImGui::Spacing();

    const float bw = 130.0f;
    ImGui::SetCursorPosX((panel_w - bw) * 0.5f);
    if (ImGui::Button("Open ROM\xe2\x80\xa6", ImVec2(bw, 0)))
        ui->open_file_dialog_requested = true;

    if (!ui->recent_roms.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("Recent:");
        for (const auto &rom : ui->recent_roms) {
            const char *label = rom.c_str();
            const char *slash = strrchr(label, '/');
            if (slash) label = slash + 1;
            if (ImGui::MenuItem(label))
                do_load_rom(ui, rom.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", rom.c_str());
        }
    }

    ImGui::End();
}

/* ---------- Public API ---------------------------------------------------- */

struct ui_context *ui_init(struct display_context *display,
                           const struct ui_callbacks *callbacks) {
    SDL_Renderer *renderer =
        static_cast<SDL_Renderer *>(display_get_renderer(display));
    SDL_Window *window =
        static_cast<SDL_Window *>(display_get_window(display));

    if (!renderer || !window) return nullptr;

    ui_context *ui            = new ui_context{};
    ui->show_demo             = false;
    ui->show_menubar          = true;
    ui->show_fps              = false;
    ui->speed_multiplier      = 1.0f;
    ui->fps                   = 0.0f;
    ui->fps_last_tick         = SDL_GetTicks();
    ui->fps_frame_count       = 0;
    ui->rom_loaded            = false;
    ui->pending_drop_path[0]  = '\0';

    if (callbacks)
        ui->callbacks = *callbacks;

    load_recent_roms(ui);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    display_set_event_hook(display, sdl_event_hook, ui);

    NFD_Init();

    return ui;
}

void ui_render_frame(struct ui_context *ui, struct display_context *display) {
    SDL_Renderer *renderer =
        static_cast<SDL_Renderer *>(display_get_renderer(display));
    SDL_Texture *game_tex =
        static_cast<SDL_Texture *>(display_get_screen_texture(display));
    int gw = display_get_width(display);
    int gh = display_get_height(display);

    display_upload_framebuffer(display);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    /* ------ Process drag-and-drop from previous event poll -------------- */
    if (ui && ui->pending_drop_path[0] != '\0') {
        do_load_rom(ui, ui->pending_drop_path);
        ui->pending_drop_path[0] = '\0';
    }

    /* ------ Keyboard shortcuts handled here (after NewFrame) ------------ */
    if (ImGui::IsKeyPressed(ImGuiKey_F1))
        ui->show_menubar = !ui->show_menubar;
    if (ImGui::IsKeyPressed(ImGuiKey_F11))
        display_toggle_fullscreen(display);

    /* ------ Demo window (dev helper) ------------------------------------ */
    if (ui && ui->show_demo)
        ImGui::ShowDemoWindow(&ui->show_demo);

    /* ------ Main menu bar ----------------------------------------------- */
    float menu_height = 0.0f;
    if (ui && ui->show_menubar) {
        if (ImGui::BeginMainMenuBar()) {
            menu_height = ImGui::GetWindowHeight();

            render_menu_file(ui, display);
            render_menu_emulation(ui, display);
            render_menu_debug(ui);
            render_menu_view(ui, display);
            render_menu_help(ui);

            /* FPS counter right-aligned in the menu bar, with speed annotation
             * when running at a non-default multiplier. */
            if (ui->show_fps) {
                char buf[40];
                if (ui->speed_multiplier < 0.0f)
                    snprintf(buf, sizeof(buf), "%.1f FPS [>>]", ui->fps);
                else if (ui->speed_multiplier != 1.0f)
                    snprintf(buf, sizeof(buf), "%.1f FPS [%.0f%%]",
                             ui->fps, ui->speed_multiplier * 100.0f);
                else
                    snprintf(buf, sizeof(buf), "%.1f FPS", ui->fps);
                float tw = ImGui::CalcTextSize(buf).x +
                           ImGui::GetStyle().ItemSpacing.x * 2.0f;
                ImGui::SetCursorPosX(ImGui::GetWindowWidth() - tw);
                ImGui::TextUnformatted(buf);
            }

            ImGui::EndMainMenuBar();
        }
    }

    /* ------ Open popups on the frame after they were requested ---------- */
    if (ui) {
        if (ui->controls_popup_requested) {
            ImGui::OpenPopup("Controls Reference##modal");
            ui->controls_popup_requested = false;
        }
        if (ui->about_popup_requested) {
            ImGui::OpenPopup("About##modal");
            ui->about_popup_requested = false;
        }

        render_controls_popup();
        render_about_popup();
    }

    /* ------ Splash overlay when no ROM is loaded ------------------------ */
    if (ui && !ui->rom_loaded)
        render_no_rom_splash(ui, display, menu_height);

    ImGui::Render();

    /* ------ Composite: game texture then ImGui on top ------------------- */
    SDL_RenderClear(renderer);
    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);

    /* Reserve space for the menu bar so the game isn't hidden behind it. */
    int avail_h = win_h - (int)menu_height;
    if (avail_h < 1) avail_h = 1;

    if (ui && ui->rom_loaded) {
        float scale = std::min((float)win_w / gw, (float)avail_h / gh);
        SDL_Rect dst = {
            (int)((win_w - gw * scale) * 0.5f),
            (int)(menu_height + (avail_h - gh * scale) * 0.5f),
            (int)(gw * scale),
            (int)(gh * scale)
        };
        SDL_RenderCopy(renderer, game_tex, nullptr, &dst);
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    /* ------ Native file dialog (opened after present so ImGui isn't mid-frame) */
    if (ui && ui->open_file_dialog_requested) {
        ui->open_file_dialog_requested = false;
        nfdchar_t *path = nullptr;
        nfdfilteritem_t filters[1] = {{"NES ROM", "nes"}};
        nfdresult_t result = NFD_OpenDialog(&path, filters, 1, nullptr);
        if (result == NFD_OKAY) {
            do_load_rom(ui, path);
            NFD_FreePath(path);
        }
    }

    /* ------ FPS tracking ------------------------------------------------ */
    if (ui) {
        ui->fps_frame_count++;
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - ui->fps_last_tick;
        if (elapsed >= 500) {
            ui->fps = ui->fps_frame_count * 1000.0f / (float)elapsed;
            ui->fps_frame_count = 0;
            ui->fps_last_tick   = now;
        }
    }
}

void ui_shutdown(struct ui_context *ui) {
    NFD_Quit();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    delete ui;
}

void ui_toggle_demo(struct ui_context *ui) {
    if (ui) ui->show_demo = !ui->show_demo;
}

float ui_get_speed_multiplier(const struct ui_context *ui) {
    return ui ? ui->speed_multiplier : 1.0f;
}

int ui_show_fps(const struct ui_context *ui) {
    return ui ? (ui->show_fps ? 1 : 0) : 0;
}

void ui_notify_rom_loaded(struct ui_context *ui, int loaded) {
    if (ui) ui->rom_loaded = (loaded != 0);
}
