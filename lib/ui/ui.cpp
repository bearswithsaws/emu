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
    std::vector<uint16_t> dbg_breakpoints;
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

    /* Disassemble DASM_LINES instructions from scroll_addr. */
    static const int DASM_LINES = 20;
    uint16_t  dasm_addrs[DASM_LINES];
    char      dasm_texts[DASM_LINES][32];
    disasm_forward(bus, ui->dbg_scroll_addr, dasm_addrs, dasm_texts, DASM_LINES);

    float line_h = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 list_sz = ImVec2(-1.0f, line_h * DASM_LINES + 4.0f);
    ImGui::BeginChild("##dasm", list_sz, ImGuiChildFlags_FrameStyle);

    ImVec4 col_pc   = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);  /* yellow — current PC */
    ImVec4 col_bp   = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  /* red — breakpoint */
    ImVec4 col_norm = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    for (int i = 0; i < DASM_LINES; i++) {
        uint16_t a = dasm_addrs[i];
        bool is_pc = (a == cpu->PC);
        bool is_bp = false;
        for (auto bp : ui->dbg_breakpoints)
            if (bp == a) { is_bp = true; break; }

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

        /* Click to toggle breakpoint. */
        if (ImGui::IsItemClicked()) {
            auto &bps = ui->dbg_breakpoints;
            auto it = std::find(bps.begin(), bps.end(), a);
            if (it != bps.end())
                bps.erase(it);
            else
                bps.push_back(a);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click to toggle breakpoint at $%04X", a);
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

    /* Breakpoint list. */
    if (!ui->dbg_breakpoints.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Breakpoints:");
        ImGui::SameLine();
        for (size_t i = 0; i < ui->dbg_breakpoints.size(); i++) {
            if (i > 0) ImGui::SameLine(0, 4);
            char bplabel[16];
            snprintf(bplabel, sizeof(bplabel), "$%04X##bp%d",
                     ui->dbg_breakpoints[i], (int)i);
            if (ImGui::SmallButton(bplabel))
                ui->dbg_breakpoints.erase(ui->dbg_breakpoints.begin() + (int)i);
        }
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

/* ---------- Memory Viewer panel (stub) ------------------------------------ */

static void render_memory_viewer(ui_context *ui, display_context *display) {
    if (!ui->show_memory_viewer) return;

    ImGui::SetNextWindowSize(ImVec2(530, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Memory Viewer", &ui->show_memory_viewer)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Memory Viewer — not yet implemented.");
    ImGui::End();
}

/* ---------- PPU Viewer panel (stub) --------------------------------------- */

static void render_ppu_viewer(ui_context *ui, display_context *display) {
    if (!ui->show_ppu_viewer) return;

    ImGui::SetNextWindowSize(ImVec2(530, 300), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("PPU Viewer", &ui->show_ppu_viewer)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("PPU Viewer — not yet implemented.");
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
    if (ui) {
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImVec2 ds_pos  = ImVec2(vp->Pos.x, vp->Pos.y + menu_height);
        ImVec2 ds_size = ImVec2(vp->Size.x, vp->Size.y - menu_height);

        ImGui::SetNextWindowPos(ds_pos);
        ImGui::SetNextWindowSize(ds_size);
        ImGui::SetNextWindowViewport(vp->ID);

        ImGuiWindowFlags ds_window_flags =
            ImGuiWindowFlags_NoTitleBar         | ImGuiWindowFlags_NoCollapse   |
            ImGuiWindowFlags_NoResize           | ImGuiWindowFlags_NoMove       |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking          | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##DockSpaceWindow", nullptr, ds_window_flags);
        ImGui::PopStyleVar();

        ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        if (!ui->dockspace_initialized) {
            setup_default_dock_layout(dockspace_id);
            ui->dockspace_initialized = true;
        }

        ImGui::End();
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

void ui_set_debug_context(struct ui_context *ui,
                          struct cpu6502 *cpu,
                          struct ppu2c02 *ppu,
                          struct nesbus  *bus) {
    if (!ui) return;
    ui->dbg_cpu = cpu;
    ui->dbg_ppu = ppu;
    ui->dbg_bus = bus;
    ui->dbg_scroll_needs_sync = true;
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
    for (auto bp : ui->dbg_breakpoints)
        if (bp == addr) return 1;
    return 0;
}
