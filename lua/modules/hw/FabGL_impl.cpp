/*
 * FabGL_impl.cpp — C++ implementation of the plain-C FabGL wrapper API.
 *
 * This file is compiled as C++ and uses FabGL directly.
 * It exposes only plain-C functions (declared in FabGL_lua.h) so that
 * FabGL.c can call them without any C++ linkage or type-conversion issues.
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_VGA

#include "fabgl.h"
#include "fonts/font_4x6.h"
#include "fonts/font_6x8.h"
#include "fonts/font_8x8.h"
#include "fonts/font_8x9.h"
#include "fonts/font_8x14.h"
#include "fonts/font_8x16.h"

#include "FabGL_lua.h"

using namespace fabgl;

/* ---------- module state ---------- */

static VGAController s_vga;
static Canvas        s_canvas(&s_vga);
static bool          s_started = false;

static const FontInfo *s_fonts[] = {
    &FONT_4x6,   /* 0 — FONT_4x6  */
    &FONT_6x8,   /* 1 — FONT_6x8  */
    &FONT_8x8,   /* 2 — FONT_8x8  */
    &FONT_8x9,   /* 3 — FONT_8x9  */
    &FONT_8x14,  /* 4 — FONT_8x14 */
    &FONT_8x16,  /* 5 — FONT_8x16 */
};
#define NUM_FONTS  (int)(sizeof(s_fonts) / sizeof(s_fonts[0]))

/* ---------- lifecycle ---------- */

void fabgl_vga_begin_8color(int r, int g, int b, int hsync, int vsync)
{
    s_vga.begin((gpio_num_t)r, (gpio_num_t)g, (gpio_num_t)b,
                (gpio_num_t)hsync, (gpio_num_t)vsync);
    s_started = true;
}

void fabgl_vga_begin_64color(int r1, int r0, int g1, int g0,
                              int b1, int b0, int hsync, int vsync)
{
    s_vga.begin((gpio_num_t)r1, (gpio_num_t)r0,
                (gpio_num_t)g1, (gpio_num_t)g0,
                (gpio_num_t)b1, (gpio_num_t)b0,
                (gpio_num_t)hsync, (gpio_num_t)vsync);
    s_started = true;
}

void fabgl_vga_set_resolution(const char *modeline, int vpw, int vph, bool dbl)
{
    s_vga.setResolution(modeline, vpw, vph, dbl);
}

void fabgl_vga_done(void)
{
    if (s_started) {
        s_vga.end();
        s_started = false;
    }
}

bool fabgl_vga_started(void)
{
    return s_started;
}

/* ---------- canvas dimensions ---------- */

int fabgl_canvas_width(void)
{
    return s_canvas.getWidth();
}

int fabgl_canvas_height(void)
{
    return s_canvas.getHeight();
}

/* ---------- paint state ---------- */

void fabgl_set_pen_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    s_canvas.setPenColor(r, g, b);
}

void fabgl_set_pen_color_idx(int color)
{
    s_canvas.setPenColor((Color)color);
}

void fabgl_set_brush_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    s_canvas.setBrushColor(r, g, b);
}

void fabgl_set_brush_color_idx(int color)
{
    s_canvas.setBrushColor((Color)color);
}

void fabgl_set_pen_width(int w)
{
    s_canvas.setPenWidth(w);
}

void fabgl_set_scrolling_region(int x1, int y1, int x2, int y2)
{
    s_canvas.setScrollingRegion(x1, y1, x2, y2);
}

/* ---------- drawing ---------- */

void fabgl_clear(void)
{
    s_canvas.clear();
}

void fabgl_set_pixel(int x, int y)
{
    s_canvas.setPixel(x, y);
}

void fabgl_get_pixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    RGB888 px = s_canvas.getPixel(x, y);
    *r = px.R;
    *g = px.G;
    *b = px.B;
}

void fabgl_move_to(int x, int y)
{
    s_canvas.moveTo(x, y);
}

void fabgl_line_to(int x, int y)
{
    s_canvas.lineTo(x, y);
}

void fabgl_draw_line(int x1, int y1, int x2, int y2)
{
    s_canvas.drawLine(x1, y1, x2, y2);
}

void fabgl_draw_rect(int x1, int y1, int x2, int y2)
{
    s_canvas.drawRectangle(x1, y1, x2, y2);
}

void fabgl_fill_rect(int x1, int y1, int x2, int y2)
{
    s_canvas.fillRectangle(x1, y1, x2, y2);
}

void fabgl_draw_ellipse(int x, int y, int w, int h)
{
    s_canvas.drawEllipse(x, y, w, h);
}

void fabgl_fill_ellipse(int x, int y, int w, int h)
{
    s_canvas.fillEllipse(x, y, w, h);
}

/* ---------- text ---------- */

void fabgl_select_font(int id)
{
    if (id >= 0 && id < NUM_FONTS)
        s_canvas.selectFont(s_fonts[id]);
}

void fabgl_draw_text(int x, int y, const char *text, bool wrap)
{
    s_canvas.drawText(x, y, text, wrap);
}

int fabgl_text_width(const char *text)
{
    return s_canvas.textExtent(text);
}

/* ---------- region / copy ---------- */

void fabgl_scroll(int dx, int dy)
{
    s_canvas.scroll(dx, dy);
}

void fabgl_copy_rect(int sx, int sy, int dx, int dy, int w, int h)
{
    s_canvas.copyRect(sx, sy, dx, dy, w, h);
}

/* ---------- sync ---------- */

void fabgl_wait_completion(bool vsync)
{
    s_canvas.waitCompletion(vsync);
}

void fabgl_swap_buffers(void)
{
    s_canvas.swapBuffers();
}

#endif /* CONFIG_LUA_RTOS_LUA_USE_VGA */
