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
#include "vendor/imgui/imgui_internal.h"
#include "vendor/imgui/backends/imgui_impl_sdl2.h"
#include "vendor/imgui/backends/imgui_impl_sdlrenderer2.h"
#include "vendor/imgui_memory_editor.h"
#include <nfd.h>

extern "C" {
#include "display.h"
#include "emu_config.h"
}

/* C emulator headers — included outside extern "C" because they are guarded
 * with their own __cplusplus checks inside (e.g. _Atomic compat). */
#include "6502.h"
#include "2c02.h"
#include "nesbus.h"
#include "disasm.h"

#define MAX_RECENT_ROMS 5

/* Debug panel layout reference widths (used in DockBuilder ratio comments).
 *
 *  ┌──────────────────────────┬──────────────┬───────────────┐
 *  │   Game##viewport         │ CPU Debugger │ Memory Viewer │
 *  │   (central node)         │  (col A top) │  (col B top)  │
 *  │                          ├──────────────┤               │
 *  │                          │ APU Visualiz ├───────────────┤
 *  │                          │  (col A bot) │  PPU Viewer   │
 *  └──────────────────────────┴──────────────┴───────────────┘
 *
 * col B = 30% of total dockspace width
 * col A = 35% of remaining width after col B is split off
 * game  = rest (central)
 */
static const int DBG_COL_A_W = 440;   /* reference width — CPU Debugger + APU  */
static const int DBG_COL_B_W = 540;   /* reference width — Memory Viewer + PPU */

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

    /* DockSpace: false until the default layout has been built once */
    bool dockspace_initialized;

    /* CPU debugger */
    struct cpu6502 *dbg_cpu;
    struct ppu2c02 *dbg_ppu;
    struct nesbus  *dbg_bus;
    uint64_t        dbg_cycles;           /* total CPU cycles, updated each frame */
    uint16_t        dbg_scroll_addr;      /* top address in disassembly view */
    bool            dbg_scroll_needs_sync;/* true when PC scrolled out of view */
    int             dbg_step_pending;
    int             dbg_step_over_pending;
    uint16_t        dbg_step_over_target;
    int             dbg_break_pending;
    struct ui_breakpoint dbg_breakpoints[UI_MAX_BREAKPOINTS];
    int                  dbg_bp_count;
    int                  dbg_rw_bp_hit;  /* set by nesbus hook, consumed by main loop */
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

    if (ImGui::BeginMenu("Save State")) {
        for (int i = 1; i <= 5; i++) {
            char label[24];
            snprintf(label, sizeof(label), "Slot %d  (F%d)", i, i + 1);
            if (ImGui::MenuItem(label) && ui->callbacks.on_save_state)
                ui->callbacks.on_save_state(i, ui->callbacks.userdata);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Load State")) {
        for (int i = 1; i <= 5; i++) {
            char label[32];
            snprintf(label, sizeof(label), "Slot %d  (Shift+F%d)", i, i + 1);
            if (ImGui::MenuItem(label) && ui->callbacks.on_load_state)
                ui->callbacks.on_load_state(i, ui->callbacks.userdata);
        }
        ImGui::EndMenu();
    }

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
        ImGui::Text("Save State  F2-F6 (slot 1-5)");
        ImGui::Text("Load State  Shift+F2-F6 (slot 1-5)");
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

/* ---------- No-ROM splash (renders inside the Game##viewport window) ------- */

static void render_no_rom_splash_content(ui_context *ui) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float panel_w = 320.0f;
    const float panel_h = 180.0f;
    float ox = (avail.x - panel_w) * 0.5f;
    float oy = (avail.y - panel_h) * 0.5f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (ox > 0 ? ox : 0),
                               ImGui::GetCursorPosY() + (oy > 0 ? oy : 0)));
    ImGui::BeginChild("##splash_inner", ImVec2(panel_w, panel_h),
                      ImGuiChildFlags_FrameStyle);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);

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

    ImGui::EndChild();
}

/* ---------- DockSpace default layout -------------------------------------- */

static void setup_default_dock_layout(ImGuiID dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_col_b, dock_col_a, dock_col_a_bot, dock_col_b_bot;

    /* Split right 30% for col B (Memory Viewer + PPU Viewer) */
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.30f,
                                &dock_col_b, &dock_main);
    /* Split remaining right 35% for col A (CPU Debugger + APU Visualizer) */
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.35f,
                                &dock_col_a, &dock_main);
    /* Split col A bottom 35% for APU Visualizer */
    ImGui::DockBuilderSplitNode(dock_col_a, ImGuiDir_Down, 0.35f,
                                &dock_col_a_bot, &dock_col_a);
    /* Split col B bottom 35% for PPU Viewer */
    ImGui::DockBuilderSplitNode(dock_col_b, ImGuiDir_Down, 0.35f,
                                &dock_col_b_bot, &dock_col_b);

    ImGui::DockBuilderDockWindow("Game##viewport", dock_main);
    ImGui::DockBuilderDockWindow("CPU Debugger",   dock_col_a);
    ImGui::DockBuilderDockWindow("APU Visualizer", dock_col_a_bot);
    ImGui::DockBuilderDockWindow("Memory Viewer",  dock_col_b);
    ImGui::DockBuilderDockWindow("PPU Viewer",     dock_col_b_bot);

    ImGui::DockBuilderFinish(dockspace_id);
}

/* ---------- Game viewport panel ------------------------------------------- */

static void render_game_viewport(ui_context *ui, display_context *display) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool visible = ImGui::Begin("Game##viewport", nullptr, flags);
    ImGui::PopStyleVar();

    if (visible) {
        if (!ui->rom_loaded) {
            render_no_rom_splash_content(ui);
        } else {
            SDL_Texture *tex =
                static_cast<SDL_Texture *>(display_get_screen_texture(display));
            if (tex) {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                int gw = display_get_width(display);
                int gh = display_get_height(display);
                float scale = std::min(avail.x / (float)gw,
                                       avail.y / (float)gh);
                ImVec2 img_sz = ImVec2(gw * scale, gh * scale);
                ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::SetCursorPos(
                    ImVec2(cursor.x + (avail.x - img_sz.x) * 0.5f,
                           cursor.y + (avail.y - img_sz.y) * 0.5f));
                ImGui::Image((ImTextureID)(intptr_t)tex, img_sz);
            }
        }
    }
    ImGui::End();
}

/* ---------- CPU Debugger panel -------------------------------------------- */

/* Read up to 3 bytes from the bus without side-effects. */
static void dbg_read3(struct nesbus *bus, uint16_t addr, uint8_t out[3]) {
    bus->debug_read(addr, out, 3);
}

/* Disassemble `count` instructions starting at `start_addr`.
 * Fills addrs[] and texts[] (caller allocates). Returns number filled. */
static int disasm_forward(struct nesbus *bus, uint16_t start_addr,
                          uint16_t *addrs, char texts[][32], int count) {
    uint16_t cur = start_addr;
    for (int i = 0; i < count; i++) {
        addrs[i] = cur;
        uint8_t bytes[3];
        dbg_read3(bus, cur, bytes);
        int len = disasm_insn(cur, bytes, texts[i], 32);
        cur = (uint16_t)(cur + len);
    }
    return count;
}

/* Find the address N instructions before `target` (approximate — walks
 * forward from an anchor to find a window that contains target). */
static uint16_t disasm_addr_before(struct nesbus *bus, uint16_t target, int n_before) {
    /* Walk backwards up to 3*n_before bytes max, then disassemble forward
     * to find the best alignment that includes target with n_before lines above. */
    uint16_t anchor = (uint16_t)(target > (uint16_t)(n_before * 3)
                                     ? target - (uint16_t)(n_before * 3)
                                     : 0);
    /* Advance anchor to within 64 addresses of target, keeping alignment. */
    uint16_t best = anchor;
    for (uint16_t a = anchor; (int)(target - a) >= 0; ) {
        uint8_t bytes[3];
        dbg_read3(bus, a, bytes);
        int len = disasm_insn(a, bytes, nullptr, 0);
        uint16_t next = (uint16_t)(a + len);
        if (next > target) break;
        if (next == target) { best = a; break; }
        /* Track the address that is n_before instructions before target. */
        best = a;
        a = next;
    }
    /* Now best is the instruction boundary just before or at target.
     * Walk back n_before steps using a small look-behind window. */
    (void)n_before;
    return best;
}

static void render_cpu_debugger(ui_context *ui, display_context *display) {
    if (!ui->show_cpu_debugger) return;

    ImGui::SetNextWindowSize(ImVec2(430, 500), ImGuiCond_FirstUseEver);

    if (!ui->dbg_cpu || !ui->dbg_ppu || !ui->dbg_bus) {
        if (ImGui::Begin("CPU Debugger", &ui->show_cpu_debugger)) {
            ImGui::TextDisabled("No debug context set.");
        }
        ImGui::End();
        return;
    }

    struct cpu6502 *cpu = ui->dbg_cpu;
    struct ppu2c02 *ppu = ui->dbg_ppu;
    struct nesbus  *bus = ui->dbg_bus;
    if (!ImGui::Begin("CPU Debugger", &ui->show_cpu_debugger)) {
        ImGui::End();
        return;
    }

    /* ---- Registers ---- */
    ImGui::SeparatorText("Registers");

    ImGui::PushItemWidth(60.0f);

    char buf[8];
    /* PC */
    snprintf(buf, sizeof(buf), "%04X", cpu->PC);
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputText("PC##reg", buf, sizeof(buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        cpu->PC = (uint16_t)strtoul(buf, nullptr, 16);
        ui->dbg_scroll_needs_sync = true;
    }
    ImGui::SameLine(0, 8);

    /* A */
    snprintf(buf, sizeof(buf), "%02X", cpu->A);
    ImGui::SetNextItemWidth(34.0f);
    if (ImGui::InputText("A##reg", buf, sizeof(buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue))
        cpu->A = (uint8_t)strtoul(buf, nullptr, 16);
    ImGui::SameLine(0, 8);

    /* X */
    snprintf(buf, sizeof(buf), "%02X", cpu->X);
    ImGui::SetNextItemWidth(34.0f);
    if (ImGui::InputText("X##reg", buf, sizeof(buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue))
        cpu->X = (uint8_t)strtoul(buf, nullptr, 16);
    ImGui::SameLine(0, 8);

    /* Y */
    snprintf(buf, sizeof(buf), "%02X", cpu->Y);
    ImGui::SetNextItemWidth(34.0f);
    if (ImGui::InputText("Y##reg", buf, sizeof(buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue))
        cpu->Y = (uint8_t)strtoul(buf, nullptr, 16);
    ImGui::SameLine(0, 8);

    /* SP */
    snprintf(buf, sizeof(buf), "%02X", cpu->sp);
    ImGui::SetNextItemWidth(34.0f);
    if (ImGui::InputText("SP##reg", buf, sizeof(buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue))
        cpu->sp = (uint8_t)strtoul(buf, nullptr, 16);

    ImGui::PopItemWidth();

    /* Status flags — coloured indicators: green=set, dark=clear */
    ImGui::Spacing();
    const char *flag_names[] = {"N","V","-","B","D","I","Z","C"};
    const uint8_t flag_bits[] = {
        cpu->flags.N, cpu->flags.V, cpu->flags.U,
        cpu->flags.B, cpu->flags.D, cpu->flags.I,
        cpu->flags.Z, cpu->flags.C
    };
    ImVec4 col_set   = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    ImVec4 col_clear = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    for (int i = 0; i < 8; i++) {
        if (i > 0) ImGui::SameLine(0, 4);
        ImGui::TextColored(flag_bits[i] ? col_set : col_clear, "%s", flag_names[i]);
    }

    /* ---- Disassembly ---- */
    ImGui::Spacing();
    ImGui::SeparatorText("Disassembly");

    /* Sync scroll to PC when paused or on step. */
    bool paused = display_is_paused(display) != 0;
    if (ui->dbg_scroll_needs_sync || paused) {
        /* Aim to show PC with ~5 lines of context above it. */
        ui->dbg_scroll_addr = disasm_addr_before(bus, cpu->PC, 5);
        ui->dbg_scroll_needs_sync = false;
    }

    /* Disassembly height fills whatever space is available, reserving the
     * controls section below.  This keeps Step/Break/status bar visible even
     * when another window is snapped below and shrinks this panel. */
    float line_h   = ImGui::GetTextLineHeightWithSpacing();
    float frame_h  = ImGui::GetFrameHeightWithSpacing();
    float sp       = ImGui::GetStyle().ItemSpacing.y;
    float ctrl_h   = frame_h * 3          /* scroll btns + SeparatorText + step btns */
                   + frame_h * 6          /* breakpoints: header + 4 rows + add btn   */
                   + line_h  * 2          /* status bar + separator                   */
                   + sp      * 6;         /* spacings                                  */
    float avail_h  = ImGui::GetContentRegionAvail().y;
    float dasm_h   = std::max(avail_h - ctrl_h, line_h * 4.0f);

    static const int DASM_MAX = 60;
    int n_lines = std::max(4, std::min(DASM_MAX, (int)((dasm_h - 4.0f) / line_h)));
    uint16_t dasm_addrs[DASM_MAX];
    char     dasm_texts[DASM_MAX][32];
    disasm_forward(bus, ui->dbg_scroll_addr, dasm_addrs, dasm_texts, n_lines);

    ImVec2 list_sz(-1.0f, dasm_h);
    ImGui::BeginChild("##dasm", list_sz, ImGuiChildFlags_FrameStyle);

    ImVec4 col_pc   = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);  /* yellow — current PC */
    ImVec4 col_bp   = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  /* red — breakpoint */
    ImVec4 col_norm = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    for (int i = 0; i < n_lines; i++) {
        uint16_t a = dasm_addrs[i];
        bool is_pc = (a == cpu->PC);
        bool is_bp = false;
        for (int bi = 0; bi < ui->dbg_bp_count; bi++) {
            auto &b = ui->dbg_breakpoints[bi];
            if (b.type == UI_BP_EXEC && b.enabled && b.addr == a) {
                is_bp = true; break;
            }
        }

        /* Raw bytes for display. */
        uint8_t raw[3];
        dbg_read3(bus, a, raw);
        uint8_t bytes_len = (uint8_t)disasm_insn(a, raw, nullptr, 0);

        char line[80];
        if (bytes_len == 1)
            snprintf(line, sizeof(line), "%04X  %02X        %s",
                     a, raw[0], dasm_texts[i]);
        else if (bytes_len == 2)
            snprintf(line, sizeof(line), "%04X  %02X %02X     %s",
                     a, raw[0], raw[1], dasm_texts[i]);
        else
            snprintf(line, sizeof(line), "%04X  %02X %02X %02X  %s",
                     a, raw[0], raw[1], raw[2], dasm_texts[i]);

        /* Prefix for current PC. */
        char label[96];
        snprintf(label, sizeof(label), "%c %s##dasm%d",
                 is_pc ? '>' : ' ', line, i);

        ImVec4 color = is_pc ? col_pc : (is_bp ? col_bp : col_norm);
        ImGui::TextColored(color, "%s", label);

        /* Click to toggle EXEC breakpoint. */
        if (ImGui::IsItemClicked()) {
            /* Search for an existing EXEC BP at this address. */
            int found = -1;
            for (int bi = 0; bi < ui->dbg_bp_count; bi++) {
                if (ui->dbg_breakpoints[bi].type == UI_BP_EXEC &&
                    ui->dbg_breakpoints[bi].addr == a) {
                    found = bi; break;
                }
            }
            if (found >= 0) {
                /* Remove it by shifting the array down. */
                for (int bi = found; bi < ui->dbg_bp_count - 1; bi++)
                    ui->dbg_breakpoints[bi] = ui->dbg_breakpoints[bi + 1];
                ui->dbg_bp_count--;
            } else if (ui->dbg_bp_count < UI_MAX_BREAKPOINTS) {
                ui->dbg_breakpoints[ui->dbg_bp_count] = {a, UI_BP_EXEC, 1};
                ui->dbg_bp_count++;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click to toggle execute breakpoint at $%04X", a);
    }

    ImGui::EndChild();

    /* Scroll buttons. */
    if (ImGui::Button("<<")) {
        uint8_t b[3]; dbg_read3(bus, ui->dbg_scroll_addr, b);
        int step = disasm_insn(ui->dbg_scroll_addr, b, nullptr, 0) * 4;
        ui->dbg_scroll_addr = (uint16_t)(ui->dbg_scroll_addr > step
                                             ? ui->dbg_scroll_addr - step
                                             : 0);
    }
    ImGui::SameLine();
    if (ImGui::Button(">>")) {
        uint8_t b[3]; dbg_read3(bus, ui->dbg_scroll_addr, b);
        int step = disasm_insn(ui->dbg_scroll_addr, b, nullptr, 0) * 4;
        ui->dbg_scroll_addr = (uint16_t)(ui->dbg_scroll_addr + step);
    }
    ImGui::SameLine();
    if (ImGui::Button("PC##scroll")) {
        ui->dbg_scroll_needs_sync = true;
    }

    /* ---- Controls ---- */
    ImGui::Spacing();
    ImGui::SeparatorText("Controls");

    bool is_paused = display_is_paused(display) != 0;

    if (ImGui::Button("Step") || ImGui::IsKeyPressed(ImGuiKey_F7)) {
        if (!ui->dbg_step_pending && !ui->dbg_step_over_pending) {
            ui->dbg_step_pending = 1;
            display_set_paused(display, 1);
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Step Over") || ImGui::IsKeyPressed(ImGuiKey_F8)) {
        if (!ui->dbg_step_pending && !ui->dbg_step_over_pending) {
            /* Compute target = PC + instruction length. */
            uint8_t bytes[3];
            dbg_read3(bus, cpu->PC, bytes);
            char tmp[32];
            int len = disasm_insn(cpu->PC, bytes, tmp, sizeof(tmp));
            ui->dbg_step_over_target  = (uint16_t)(cpu->PC + len);
            ui->dbg_step_over_pending = 1;
            display_set_paused(display, 1);
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Run")) {
        ui->dbg_step_pending      = 0;
        ui->dbg_step_over_pending = 0;
        display_set_paused(display, 0);
    }
    ImGui::SameLine();

    if (ImGui::Button("Break")) {
        ui->dbg_break_pending = 1;
        display_set_paused(display, 1);
    }
    ImGui::SameLine();

    if (ImGui::Button("Reset")) {
        if (ui->callbacks.on_soft_reset)
            ui->callbacks.on_soft_reset(ui->callbacks.userdata);
    }

    /* ---- Breakpoint table ---- */
    ImGui::Spacing();
    ImGui::SeparatorText("Breakpoints");

    static const char *type_names[] = { "Exec", "Read", "Write" };

    float bp_table_h = frame_h * 4.5f;   /* room for header + ~3 rows */
    ImGui::BeginChild("##bpchild", ImVec2(-1.0f, bp_table_h), ImGuiChildFlags_None);

    if (ImGui::BeginTable("##bptable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY  |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed,   48.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed,   56.0f);
        ImGui::TableSetupColumn("En",      ImGuiTableColumnFlags_WidthFixed,   24.0f);
        ImGui::TableSetupColumn("Hit",     ImGuiTableColumnFlags_WidthFixed,   24.0f);
        ImGui::TableSetupColumn("Del",     ImGuiTableColumnFlags_WidthFixed,   24.0f);
        ImGui::TableHeadersRow();

        int to_delete = -1;
        for (int bi = 0; bi < ui->dbg_bp_count; bi++) {
            struct ui_breakpoint &bp = ui->dbg_breakpoints[bi];
            ImGui::TableNextRow();

            /* Type dropdown */
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            char combo_id[16];
            snprintf(combo_id, sizeof(combo_id), "##bptype%d", bi);
            if (ImGui::BeginCombo(combo_id, type_names[bp.type])) {
                for (int t = 0; t < 3; t++) {
                    bool sel = (bp.type == (ui_bp_type)t);
                    if (ImGui::Selectable(type_names[t], sel))
                        bp.type = (ui_bp_type)t;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            /* Address hex input */
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            char addr_buf[8];
            snprintf(addr_buf, sizeof(addr_buf), "%04X", bp.addr);
            char addr_id[16];
            snprintf(addr_id, sizeof(addr_id), "##bpaddr%d", bi);
            if (ImGui::InputText(addr_id, addr_buf, sizeof(addr_buf),
                                 ImGuiInputTextFlags_CharsHexadecimal |
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                unsigned v = 0;
                sscanf(addr_buf, "%X", &v);
                bp.addr = (uint16_t)(v & 0xFFFF);
            }

            /* Enabled checkbox */
            ImGui::TableSetColumnIndex(2);
            bool en = (bp.enabled != 0);
            char en_id[16];
            snprintf(en_id, sizeof(en_id), "##bpen%d", bi);
            if (ImGui::Checkbox(en_id, &en))
                bp.enabled = en ? 1 : 0;

            /* Hit indicator (red dot when type != EXEC and rw_bp_hit) */
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("-");

            /* Delete button */
            ImGui::TableSetColumnIndex(4);
            char del_id[16];
            snprintf(del_id, sizeof(del_id), "X##bpdel%d", bi);
            if (ImGui::SmallButton(del_id))
                to_delete = bi;
        }

        ImGui::EndTable();

        if (to_delete >= 0) {
            for (int bi = to_delete; bi < ui->dbg_bp_count - 1; bi++)
                ui->dbg_breakpoints[bi] = ui->dbg_breakpoints[bi + 1];
            ui->dbg_bp_count--;
        }
    }

    ImGui::EndChild();

    if (ui->dbg_bp_count < UI_MAX_BREAKPOINTS) {
        if (ImGui::SmallButton("+ Add Breakpoint")) {
            ui->dbg_breakpoints[ui->dbg_bp_count] = {0x0000, UI_BP_EXEC, 1};
            ui->dbg_bp_count++;
        }
    } else {
        ImGui::TextDisabled("(max %d breakpoints)", UI_MAX_BREAKPOINTS);
    }

    /* ---- Status bar ---- */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("CYC:%-10llu  SL:%-3d  DOT:%-3d  %s",
                (unsigned long long)ui->dbg_cycles,
                (int)ppu->scanline,
                (int)ppu->dot,
                is_paused ? "[PAUSED]" : "[RUNNING]");

    ImGui::End();
}

/* ---------- APU Visualizer panel (stub) ----------------------------------- */

/* ---------- NES system palette (NTSC 2C02 approximation, ARGB8888) -------- */

static const uint32_t NES_SYS_PALETTE[64] = {
    0xFF545454, 0xFF001E74, 0xFF081090, 0xFF300088,
    0xFF440064, 0xFF5C0030, 0xFF540400, 0xFF3C1800,
    0xFF202A00, 0xFF083A00, 0xFF004000, 0xFF003C00,
    0xFF00323C, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFF989698, 0xFF084CC4, 0xFF3032EC, 0xFF5C1EE4,
    0xFF8814B0, 0xFFA01464, 0xFF982220, 0xFF783C00,
    0xFF545A00, 0xFF287200, 0xFF087C00, 0xFF007628,
    0xFF006678, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFF4C9AEC, 0xFF787CEC, 0xFFB062EC,
    0xFFE454EC, 0xFFEC58B4, 0xFFEC6A64, 0xFFD48820,
    0xFFA0AA00, 0xFF74C400, 0xFF4CD020, 0xFF38CC6C,
    0xFF38B4CC, 0xFF3C3C3C, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFFA8CCEC, 0xFFBCBCEC, 0xFFD4B2EC,
    0xFFECAEEC, 0xFFECAED4, 0xFFECB4B0, 0xFFE4C490,
    0xFFCCD278, 0xFFB4DE78, 0xFFA8E290, 0xFF98E2B4,
    0xFFA0D6E4, 0xFFA0A2A0, 0xFF000000, 0xFF000000,
};

/* Decode one 8×8 CHR tile into an ARGB8888 pixel buffer.
 * tile_data: 16-byte tile (bytes 0-7 = low bitplane, 8-15 = high bitplane)
 * pal_ram:   ppu->palette_table (32 bytes)
 * pal_slot:  0-3 = BG palette, 4-7 = sprite palette
 * pixels:    output buffer with given stride (uint32_t elements per row) */
static void ppu_decode_tile(const uint8_t *tile_data, const uint8_t *pal_ram,
                             int pal_slot, uint32_t *pixels, int stride) {
    int pal_base = (pal_slot < 4) ? (pal_slot * 4) : ((pal_slot - 4) * 4 + 0x10);
    for (int row = 0; row < 8; row++) {
        uint8_t lo = tile_data[row];
        uint8_t hi = tile_data[row + 8];
        for (int col = 0; col < 8; col++) {
            int bit   = 7 - col;
            int pixel = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            uint8_t ci = pal_ram[pal_base + pixel] & 0x3F;
            pixels[row * stride + col] = NES_SYS_PALETTE[ci];
        }
    }
}

static void render_apu_visualizer(ui_context *ui, display_context *display) {
    if (!ui->show_apu_visualizer) return;

    ImGui::SetNextWindowSize(ImVec2(430, 200), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("APU Visualizer", &ui->show_apu_visualizer)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("APU Visualizer — not yet implemented.");
    ImGui::End();
}

/* ---------- Memory Viewer panel ------------------------------------------- */

/* CPU bus read/write callbacks for MemoryEditor — uses nesbus debug_read/write
 * so all bus side-effects (mapper, PPU register reads) happen normally.
 * mem_data is unused; the bus pointer is passed through UserData. */
static ImU8 memview_cpu_read(const ImU8 * /*mem*/, size_t off, void *user_data) {
    struct nesbus *bus = static_cast<struct nesbus *>(user_data);
    uint8_t byte = 0;
    bus->debug_read((uint16_t)off, &byte, 1);
    return byte;
}
static void memview_cpu_write(ImU8 * /*mem*/, size_t off, ImU8 d, void *user_data) {
    struct nesbus *bus = static_cast<struct nesbus *>(user_data);
    bus->write((uint16_t)off, (uint8_t)d);
}

/* Background color hints for the CPU address space. */
static ImU32 memview_cpu_bg(const ImU8 * /*mem*/, size_t off, void * /*u*/) {
    if (off < 0x0100) return IM_COL32(60,  90, 160,  80);  /* zero page — blue   */
    if (off < 0x0200) return IM_COL32(90,  60, 160,  80);  /* stack     — purple */
    if (off < 0x0800) return IM_COL32(50,  50,  50,  50);  /* RAM                */
    if (off >= 0x2000 && off < 0x2008)
                      return IM_COL32(50, 140,  50,  80);  /* PPU regs  — green  */
    if (off >= 0x4000 && off < 0x4018)
                      return IM_COL32(160, 100, 40,  80);  /* APU/IO    — orange */
    if (off >= 0x8000) return IM_COL32(100, 70,  30,  60); /* ROM       — tan    */
    return IM_COL32(0, 0, 0, 0);
}

static void render_memory_viewer(ui_context *ui, display_context *display) {
    if (!ui->show_memory_viewer) return;

    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Memory Viewer", &ui->show_memory_viewer)) {
        ImGui::End();
        return;
    }

    if (!ui->dbg_cpu || !ui->dbg_ppu || !ui->dbg_bus) {
        ImGui::TextDisabled("No debug context set.");
        ImGui::End();
        return;
    }

    struct ppu2c02 *ppu = ui->dbg_ppu;
    struct nesbus  *bus = ui->dbg_bus;

    /* One persistent MemoryEditor per segment so scroll/cursor survive tab switches. */
    static MemoryEditor mem_cpu, mem_oam, mem_vram, mem_palette;
    static bool mem_init = false;
    if (!mem_init) {
        mem_cpu.ReadFn    = memview_cpu_read;
        mem_cpu.WriteFn   = memview_cpu_write;
        mem_cpu.BgColorFn = memview_cpu_bg;
        mem_init = true;
    }
    mem_cpu.UserData = bus;   /* refresh each frame in case bus pointer changes */

    /* Segment tab bar */
    static int seg = 0;
    if (ImGui::BeginTabBar("##memsegs")) {
        if (ImGui::BeginTabItem("CPU Bus"))  { seg = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("OAM"))      { seg = 1; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("PPU VRAM")) { seg = 2; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Palette"))  { seg = 3; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    switch (seg) {
    case 0:
        /* CPU bus — 64 KB via custom read/write callbacks; pass null for mem_data
         * since ReadFn/WriteFn never dereference it. */
        mem_cpu.DrawContents(nullptr, 0x10000, 0x0000);
        break;
    case 1:
        /* OAM — 256 bytes, directly editable */
        mem_oam.DrawContents(ppu->oam, 256, 0x0000);
        break;
    case 2:
        /* PPU nametable VRAM — 2 KB, base address $2000 */
        mem_vram.DrawContents(ppu->nametable, 0x0800, 0x2000);
        break;
    case 3:
        /* Palette RAM — 32 bytes, base address $3F00 */
        mem_palette.DrawContents(ppu->palette_table, 0x20, 0x3F00);
        break;
    }

    ImGui::End();
}

/* ---------- PPU Viewer panel (#54 Pattern Tables, #55 Nametables, #56 OAM) */

static void render_ppu_viewer(ui_context *ui, display_context *display) {
    if (!ui->show_ppu_viewer) return;

    ImGui::SetNextWindowSize(ImVec2(600, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("PPU Viewer", &ui->show_ppu_viewer)) {
        ImGui::End();
        return;
    }

    if (!ui->dbg_ppu) {
        ImGui::TextDisabled("No debug context set.");
        ImGui::End();
        return;
    }

    struct ppu2c02  *ppu      = ui->dbg_ppu;
    SDL_Renderer    *renderer =
        static_cast<SDL_Renderer *>(display_get_renderer(display));

    /* Static SDL textures — created once on first open, freed at SDL shutdown */
    static SDL_Texture *pt_tex[2] = {nullptr, nullptr}; /* 128×128 pattern tables */
    static SDL_Texture *nt_tex[4] = {nullptr, nullptr, nullptr, nullptr}; /* 256×240 nametables */
    if (!pt_tex[0]) {
        for (int i = 0; i < 2; i++)
            pt_tex[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, 128, 128);
    }
    if (!nt_tex[0]) {
        for (int i = 0; i < 4; i++)
            nt_tex[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, 256, 240);
    }

    /* Throttle texture rebuilds: refresh at ~15 fps while running, every
     * frame while paused so the user sees live state when stepping.
     * This avoids paying 8K mapper reads + SDL_LockTexture GPU syncs at 60 Hz. */
    static int  refresh_ticker = 0;
    static int  pt_pal_prev    = -1; /* force refresh on palette change */
    bool        emu_paused     = display_is_paused(display) != 0;
    bool        should_refresh = emu_paused || (++refresh_ticker >= 4);
    if (should_refresh) refresh_ticker = 0;

    static int ppu_tab = 0;
    if (ImGui::BeginTabBar("##ppusegs")) {
        if (ImGui::BeginTabItem("Pattern Tables")) { ppu_tab = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Nametables"))     { ppu_tab = 1; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("OAM"))            { ppu_tab = 2; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    /* ------------------------------------------------------------------ */
    /* Pattern Tables tab                                                   */
    /* ------------------------------------------------------------------ */
    if (ppu_tab == 0) {
        static int pt_pal = 0;

        ImGui::Text("Palette:");
        const char *pal_labels[] = {
            "BG 0", "BG 1", "BG 2", "BG 3",
            "Spr 0", "Spr 1", "Spr 2", "Spr 3"
        };
        for (int i = 0; i < 8; i++) {
            ImGui::SameLine();
            if (ImGui::RadioButton(pal_labels[i], pt_pal == i)) pt_pal = i;
        }
        ImGui::Spacing();

        /* Force a refresh when the palette selection changes even mid-throttle. */
        bool pal_changed = (pt_pal != pt_pal_prev);
        if (should_refresh || pal_changed) {
            pt_pal_prev = pt_pal;

            /* Snapshot CHR only when we're about to redraw. */
            uint8_t chr_snapshot[0x2000];
            if (ppu->cart && ppu->cart->ppu_read) {
                for (int i = 0; i < 0x2000; i++)
                    chr_snapshot[i] = ppu->cart->ppu_read(ppu->cart, (uint16_t)i);
            } else {
                memcpy(chr_snapshot, ppu->pattern_table, 0x2000);
            }

            for (int tbl = 0; tbl < 2; tbl++) {
                void *pv; int pitch;
                if (SDL_LockTexture(pt_tex[tbl], nullptr, &pv, &pitch) == 0) {
                    int stride   = pitch / 4;
                    uint32_t *px = static_cast<uint32_t *>(pv);
                    for (int tile = 0; tile < 256; tile++) {
                        int tc            = tile % 16;
                        int tr            = tile / 16;
                        const uint8_t *td = chr_snapshot + tbl * 0x1000 + tile * 16;
                        uint32_t *dst     = px + tr * 8 * stride + tc * 8;
                        ppu_decode_tile(td, ppu->palette_table, pt_pal, dst, stride);
                    }
                    SDL_UnlockTexture(pt_tex[tbl]);
                }
            }
        }

        const float pt_scale = 2.0f;
        ImVec2 pt_sz(128 * pt_scale, 128 * pt_scale);
        for (int tbl = 0; tbl < 2; tbl++) {
            if (tbl > 0) ImGui::SameLine(0, 16);
            ImGui::BeginGroup();
            ImGui::Text("Table %d  ($%04X)", tbl, tbl * 0x1000);
            ImVec2 tex_origin = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)(intptr_t)pt_tex[tbl], pt_sz);
            if (ImGui::IsItemHovered()) {
                ImVec2 mp = ImGui::GetMousePos();
                int tx = (int)((mp.x - tex_origin.x) / (8 * pt_scale));
                int ty = (int)((mp.y - tex_origin.y) / (8 * pt_scale));
                if (tx >= 0 && tx < 16 && ty >= 0 && ty < 16) {
                    int idx = ty * 16 + tx;
                    ImGui::SetTooltip("Table %d  Tile $%02X\n$%04X\xe2\x80\x93$%04X",
                        tbl, idx,
                        tbl * 0x1000 + idx * 16,
                        tbl * 0x1000 + idx * 16 + 15);
                }
            }
            ImGui::EndGroup();
        }

    /* ------------------------------------------------------------------ */
    /* Nametables tab                                                        */
    /* ------------------------------------------------------------------ */
    } else if (ppu_tab == 1) {
        uint8_t mirror = MIRROR_HORIZONTAL;
        if (ppu->cart && ppu->cart->map) mirror = ppu->cart->map->mirroring;
        static const char *mirror_names[] = {
            "Horizontal", "Vertical", "Single-Screen Lo", "Single-Screen Hi"
        };
        ImGui::Text("Mirroring: %s", mirror_names[mirror < 4 ? mirror : 0]);
        ImGui::SameLine(0, 24);
        ImGui::TextDisabled("BG pattern table: %d", ppu->ppuctrl.bg_pattern_table);
        ImGui::Spacing();

        /* Physical VRAM offset (0x000 or 0x400) for each logical NT */
        static const uint16_t nt_off[4][4] = {
            { 0x000, 0x000, 0x400, 0x400 }, /* HORIZONTAL */
            { 0x000, 0x400, 0x000, 0x400 }, /* VERTICAL   */
            { 0x000, 0x000, 0x000, 0x000 }, /* SINGLE_LO  */
            { 0x400, 0x400, 0x400, 0x400 }, /* SINGLE_HI  */
        };
        uint8_t bg_tbl = ppu->ppuctrl.bg_pattern_table;
        uint8_t m      = mirror < 4 ? mirror : 0;

        if (should_refresh) {
            /* Snapshot CHR only when we're about to redraw. */
            uint8_t chr_snapshot[0x2000];
            if (ppu->cart && ppu->cart->ppu_read) {
                for (int i = 0; i < 0x2000; i++)
                    chr_snapshot[i] = ppu->cart->ppu_read(ppu->cart, (uint16_t)i);
            } else {
                memcpy(chr_snapshot, ppu->pattern_table, 0x2000);
            }

            for (int nt = 0; nt < 4; nt++) {
                void *pv; int pitch;
                if (SDL_LockTexture(nt_tex[nt], nullptr, &pv, &pitch) != 0) continue;
                int stride          = pitch / 4;
                uint32_t *px        = static_cast<uint32_t *>(pv);
                const uint8_t *ntd  = ppu->nametable + nt_off[m][nt];
                const uint8_t *attr = ntd + 0x3C0;

                for (int cy = 0; cy < 30; cy++) {
                    for (int cx = 0; cx < 32; cx++) {
                        uint8_t tile_id   = ntd[cy * 32 + cx];
                        uint8_t ab        = attr[(cy / 4) * 8 + (cx / 4)];
                        int shift         = ((cy & 2) << 1) | (cx & 2);
                        int pal_slot      = (ab >> shift) & 3;
                        const uint8_t *td = chr_snapshot + bg_tbl * 0x1000 + tile_id * 16;
                        uint32_t *dst     = px + cy * 8 * stride + cx * 8;
                        ppu_decode_tile(td, ppu->palette_table, pal_slot, dst, stride);
                    }
                }
                SDL_UnlockTexture(nt_tex[nt]);
            }
        }

        /* 2×2 grid at 45% scale — capture screen positions for scroll rect */
        const float nt_scale = 0.45f;
        ImVec2 nt_sz(256 * nt_scale, 240 * nt_scale);
        ImVec2 nt_pos[4];

        nt_pos[0] = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)nt_tex[0], nt_sz);
        ImGui::SameLine(0, 1);
        nt_pos[1] = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)nt_tex[1], nt_sz);

        nt_pos[2] = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)nt_tex[2], nt_sz);
        ImGui::SameLine(0, 1);
        nt_pos[3] = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)nt_tex[3], nt_sz);

        /* Scroll viewport rectangle decoded from loopy v + fine-x registers */
        if (ppu->cart) {
            uint16_t v   = ppu->v;
            int ntx      = (v >> 10) & 1;
            int nty      = (v >> 11) & 1;
            int coarse_x = v & 0x1F;
            int coarse_y = (v >> 5) & 0x1F;
            int fine_y   = (v >> 12) & 7;

            ImVec2 base  = nt_pos[nty * 2 + ntx];
            float  off_x = (coarse_x * 8 + ppu->x) * nt_scale;
            float  off_y = (coarse_y * 8 + fine_y)  * nt_scale;
            ImVec2 r0(base.x + off_x, base.y + off_y);
            ImVec2 r1(r0.x + 256 * nt_scale, r0.y + 240 * nt_scale);
            ImGui::GetWindowDrawList()->AddRect(r0, r1, IM_COL32(255, 255, 0, 220), 0, 0, 1.5f);
        }

    /* ------------------------------------------------------------------ */
    /* OAM tab                                                               */
    /* ------------------------------------------------------------------ */
    } else {
        ImGui::Text("Sprite size: %s", ppu->ppuctrl.sprite_size ? "8x16" : "8x8");
        ImGui::Text("Sprite pattern table: %d  ($%04X)",
            ppu->ppuctrl.sprite_pattern_table,
            ppu->ppuctrl.sprite_pattern_table ? 0x1000 : 0x0000);
        ImGui::Spacing();

        ImGuiTableFlags tflags =
            ImGuiTableFlags_Borders      | ImGuiTableFlags_RowBg      |
            ImGuiTableFlags_ScrollY      | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##oam", 9, tflags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed, 28);
            ImGui::TableSetupColumn("Y",    ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableSetupColumn("X",    ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableSetupColumn("Tile", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Pal",  ImGuiTableColumnFlags_WidthFixed, 28);
            ImGui::TableSetupColumn("Pri",  ImGuiTableColumnFlags_WidthFixed, 35);
            ImGui::TableSetupColumn("H-Fl", ImGuiTableColumnFlags_WidthFixed, 35);
            ImGui::TableSetupColumn("V-Fl", ImGuiTableColumnFlags_WidthFixed, 35);
            ImGui::TableSetupColumn("Vis",  ImGuiTableColumnFlags_WidthFixed, 28);
            ImGui::TableHeadersRow();

            for (int i = 0; i < 64; i++) {
                uint8_t y    = ppu->oam[i * 4 + 0];
                uint8_t tile = ppu->oam[i * 4 + 1];
                uint8_t attr = ppu->oam[i * 4 + 2];
                uint8_t x    = ppu->oam[i * 4 + 3];
                bool visible = (y < 239);

                ImGui::TableNextRow();
                if (!visible)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                          IM_COL32(50, 50, 50, 100));

                ImGui::TableNextColumn(); ImGui::Text("%d", i);
                ImGui::TableNextColumn(); ImGui::Text("%d", y);
                ImGui::TableNextColumn(); ImGui::Text("%d", x);
                ImGui::TableNextColumn(); ImGui::Text("$%02X", tile);
                ImGui::TableNextColumn(); ImGui::Text("%d", attr & 3);
                ImGui::TableNextColumn(); ImGui::Text("%s", (attr >> 5) & 1 ? "BG" : "FG");
                ImGui::TableNextColumn(); ImGui::Text("%s", (attr >> 6) & 1 ? "Y" : "-");
                ImGui::TableNextColumn(); ImGui::Text("%s", (attr >> 7) & 1 ? "Y" : "-");
                ImGui::TableNextColumn(); ImGui::Text("%s", visible     ? "Y" : "-");
            }
            ImGui::EndTable();
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
    ui->dockspace_initialized = false;

    ui->dbg_cpu               = nullptr;
    ui->dbg_ppu               = nullptr;
    ui->dbg_bus               = nullptr;
    ui->dbg_cycles            = 0;
    ui->dbg_scroll_addr       = 0;
    ui->dbg_scroll_needs_sync = true;
    ui->dbg_step_pending      = 0;
    ui->dbg_step_over_pending = 0;
    ui->dbg_step_over_target  = 0;
    ui->dbg_break_pending     = 0;

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

    /* F2–F6 = save slot 1–5,  Shift+F2–F6 = load slot 1–5 */
    {
        static const ImGuiKey save_keys[] = {
            ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5, ImGuiKey_F6 };
        bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
                     ImGui::IsKeyDown(ImGuiKey_RightShift);
        for (int i = 0; i < 5; i++) {
            if (ImGui::IsKeyPressed(save_keys[i])) {
                if (shift) {
                    if (ui->callbacks.on_load_state)
                        ui->callbacks.on_load_state(i + 1, ui->callbacks.userdata);
                } else {
                    if (ui->callbacks.on_save_state)
                        ui->callbacks.on_save_state(i + 1, ui->callbacks.userdata);
                }
            }
        }
    }

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

    /* ------ Fullscreen DockSpace covering the area below the menu bar --- */
    /* Use DockSpaceOverViewport (the official path) so that outer-edge snap
     * targets appear at all four sides of the window.  We temporarily shrink
     * the viewport's work area by the menu-bar height so the dockspace starts
     * below the menu bar, then restore it immediately after the call. */
    if (ui) {
        ImGuiViewport *vp    = ImGui::GetMainViewport();
        ImVec2 saved_work_pos  = vp->WorkPos;
        ImVec2 saved_work_size = vp->WorkSize;
        vp->WorkPos.y  += menu_height;
        vp->WorkSize.y -= menu_height;

        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        ImGui::DockSpaceOverViewport(dockspace_id, vp, ImGuiDockNodeFlags_None);

        vp->WorkPos  = saved_work_pos;
        vp->WorkSize = saved_work_size;

        if (!ui->dockspace_initialized) {
            setup_default_dock_layout(dockspace_id);
            ui->dockspace_initialized = true;
        }
    }

    /* ------ Game viewport panel ----------------------------------------- */
    if (ui)
        render_game_viewport(ui, display);

    /* ------ Debug panels ------------------------------------------------- */
    if (ui) {
        render_cpu_debugger(ui, display);
        render_apu_visualizer(ui, display);
        render_memory_viewer(ui, display);
        render_ppu_viewer(ui, display);
    }

    ImGui::Render();

    /* ------ Composite: clear, then ImGui (which includes the game image) - */
    SDL_RenderClear(renderer);
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

/* Breakpoint hook called by nesbus on every CPU read/write. */
static void bp_hook(uint16_t addr, int is_write, void *ud) {
    ui_context *ui = static_cast<ui_context *>(ud);
    if (!ui) return;
    ui_bp_type want = is_write ? UI_BP_WRITE : UI_BP_READ;
    for (int i = 0; i < ui->dbg_bp_count; i++) {
        const struct ui_breakpoint &bp = ui->dbg_breakpoints[i];
        if (bp.type == want && bp.enabled && bp.addr == addr) {
            ui->dbg_rw_bp_hit = 1;
            return;
        }
    }
}

void ui_set_debug_context(struct ui_context *ui,
                          struct cpu6502 *cpu,
                          struct ppu2c02 *ppu,
                          struct nesbus  *bus) {
    if (!ui) return;
    ui->dbg_cpu = cpu;
    ui->dbg_ppu = ppu;
    ui->dbg_bus = bus;
    ui->dbg_scroll_needs_sync = true;

    if (bus) {
        bus->bp_hook    = bp_hook;
        bus->bp_hook_ud = ui;
    }
}

void ui_debugger_update_cycles(struct ui_context *ui, uint64_t cycles) {
    if (ui) ui->dbg_cycles = cycles;
}

int ui_debugger_consume_step(struct ui_context *ui) {
    if (!ui || !ui->dbg_step_pending) return 0;
    ui->dbg_step_pending = 0;
    return 1;
}

int ui_debugger_consume_step_over(struct ui_context *ui, uint16_t *out_target) {
    if (!ui || !ui->dbg_step_over_pending) return 0;
    if (out_target) *out_target = ui->dbg_step_over_target;
    ui->dbg_step_over_pending = 0;
    return 1;
}

int ui_debugger_consume_break(struct ui_context *ui) {
    if (!ui || !ui->dbg_break_pending) return 0;
    ui->dbg_break_pending = 0;
    return 1;
}

int ui_debugger_is_breakpoint(struct ui_context *ui, uint16_t addr) {
    if (!ui) return 0;
    for (int i = 0; i < ui->dbg_bp_count; i++) {
        const struct ui_breakpoint &bp = ui->dbg_breakpoints[i];
        if (bp.type == UI_BP_EXEC && bp.enabled && bp.addr == addr) return 1;
    }
    return 0;
}

int ui_debugger_consume_rw_bp_hit(struct ui_context *ui) {
    if (!ui || !ui->dbg_rw_bp_hit) return 0;
    ui->dbg_rw_bp_hit = 0;
    return 1;
}
