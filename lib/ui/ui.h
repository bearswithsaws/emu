#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

struct display_context;

struct ui_context;

/**
 * Initialize Dear ImGui with the SDL2 + SDL_Renderer backend.
 * Must be called after display_init().
 */
struct ui_context *ui_init(struct display_context *display);

/**
 * Render one frame: uploads the NES framebuffer, runs ImGui, presents.
 * Replaces display_render_frame() in the main loop.
 */
void ui_render_frame(struct ui_context *ui, struct display_context *display);

/**
 * Shut down ImGui and free the context.
 */
void ui_shutdown(struct ui_context *ui);

/**
 * Toggle the ImGui demo window (useful during development).
 */
void ui_toggle_demo(struct ui_context *ui);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
