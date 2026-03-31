/*
 * FabGL_lua.h — plain-C interface between FabGL (C++) and the Lua vga module (C)
 *
 * Implemented in FabGL_impl.cpp; consumed by FabGL.c.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle */
void fabgl_vga_begin_8color(int r, int g, int b, int hsync, int vsync);
void fabgl_vga_begin_64color(int r1, int r0, int g1, int g0,
                              int b1, int b0, int hsync, int vsync);
void fabgl_vga_set_resolution(const char *modeline, int vpw, int vph, bool dbl);
void fabgl_vga_done(void);
bool fabgl_vga_started(void);

/* Canvas dimensions */
int  fabgl_canvas_width(void);
int  fabgl_canvas_height(void);

/* Paint state */
void fabgl_set_pen_color_rgb(uint8_t r, uint8_t g, uint8_t b);
void fabgl_set_pen_color_idx(int color);
void fabgl_set_brush_color_rgb(uint8_t r, uint8_t g, uint8_t b);
void fabgl_set_brush_color_idx(int color);
void fabgl_set_pen_width(int w);
void fabgl_set_scrolling_region(int x1, int y1, int x2, int y2);

/* Drawing */
void fabgl_clear(void);
void fabgl_set_pixel(int x, int y);
void fabgl_get_pixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b);
void fabgl_move_to(int x, int y);
void fabgl_line_to(int x, int y);
void fabgl_draw_line(int x1, int y1, int x2, int y2);
void fabgl_draw_rect(int x1, int y1, int x2, int y2);
void fabgl_fill_rect(int x1, int y1, int x2, int y2);
void fabgl_draw_ellipse(int x, int y, int w, int h);
void fabgl_fill_ellipse(int x, int y, int w, int h);

/* Text */
void fabgl_select_font(int id);
void fabgl_draw_text(int x, int y, const char *text, bool wrap);
int  fabgl_text_width(const char *text);

/* Region / copy */
void fabgl_scroll(int dx, int dy);
void fabgl_copy_rect(int sx, int sy, int dx, int dy, int w, int h);

/* Sync */
void fabgl_wait_completion(bool vsync);
void fabgl_swap_buffers(void);

#ifdef __cplusplus
}
#endif
