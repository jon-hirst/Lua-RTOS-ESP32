/*
 * FabGL.c — Lua "vga" module (plain C)
 *
 * Calls through FabGL_lua.h / FabGL_impl.cpp for all FabGL operations.
 * Kept in plain C so that the Lua LRO_FUNCVAL / LFUNCVAL macros
 * (which store function pointers as void*) compile without errors.
 *
 * Lua usage example:
 *   local vga = require("vga")
 *   vga.begin(21, 22, 19, 18, 5, 4, 23, 15)   -- 64-colour
 *   vga.setResolution('"640x480@60Hz" 25.175 640 656 752 800 480 490 492 525 -HSync -VSync')
 *   vga.setPenColor(255, 255, 0)
 *   vga.setBrushColor(vga.Black)
 *   vga.clear()
 *   vga.selectFont(vga.FONT_8x16)
 *   vga.drawText(10, 10, "Hello VGA!")
 *   vga.waitCompletion()
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_VGA

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "modules.h"

#include "FabGL_lua.h"

/* ---------- lifecycle ---------- */

/*
 * vga.begin(r1, r0, g1, g0, b1, b0, hsync, vsync)   64-colour (8 GPIO)
 * vga.begin(r, g, b, hsync, vsync)                    8-colour  (5 GPIO)
 */
static int lvga_begin(lua_State *L)
{
    int n = lua_gettop(L);
    if (n == 5) {
        fabgl_vga_begin_8color(
            (int)luaL_checkinteger(L, 1),
            (int)luaL_checkinteger(L, 2),
            (int)luaL_checkinteger(L, 3),
            (int)luaL_checkinteger(L, 4),
            (int)luaL_checkinteger(L, 5));
    } else if (n == 8) {
        fabgl_vga_begin_64color(
            (int)luaL_checkinteger(L, 1),
            (int)luaL_checkinteger(L, 2),
            (int)luaL_checkinteger(L, 3),
            (int)luaL_checkinteger(L, 4),
            (int)luaL_checkinteger(L, 5),
            (int)luaL_checkinteger(L, 6),
            (int)luaL_checkinteger(L, 7),
            (int)luaL_checkinteger(L, 8));
    } else {
        luaL_error(L, "vga.begin: expected 5 (8-colour) or 8 (64-colour) GPIO arguments");
    }
    return 0;
}

/*
 * vga.setResolution(modeline [, viewPortWidth, viewPortHeight [, doubleBuffer]])
 */
static int lvga_setResolution(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    const char *ml  = luaL_checkstring(L, 1);
    int         vpw = (int)luaL_optinteger(L, 2, -1);
    int         vph = (int)luaL_optinteger(L, 3, -1);
    int         dbl = lua_toboolean(L, 4);
    fabgl_vga_set_resolution(ml, vpw, vph, (bool)dbl);
    return 0;
}

/* vga.done() — stop VGA output */
static int lvga_done(lua_State *L)
{
    (void)L;
    fabgl_vga_done();
    return 0;
}

/* ---------- canvas dimensions ---------- */

static int lvga_width(lua_State *L)
{
    lua_pushinteger(L, fabgl_canvas_width());
    return 1;
}

static int lvga_height(lua_State *L)
{
    lua_pushinteger(L, fabgl_canvas_height());
    return 1;
}

/* ---------- paint state ---------- */

/*
 * vga.setPenColor(r, g, b)
 * vga.setPenColor(color_constant)
 */
static int lvga_setPenColor(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    if (lua_gettop(L) >= 3) {
        fabgl_set_pen_color_rgb(
            (uint8_t)luaL_checkinteger(L, 1),
            (uint8_t)luaL_checkinteger(L, 2),
            (uint8_t)luaL_checkinteger(L, 3));
    } else {
        fabgl_set_pen_color_idx((int)luaL_checkinteger(L, 1));
    }
    return 0;
}

/*
 * vga.setBrushColor(r, g, b)
 * vga.setBrushColor(color_constant)
 */
static int lvga_setBrushColor(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    if (lua_gettop(L) >= 3) {
        fabgl_set_brush_color_rgb(
            (uint8_t)luaL_checkinteger(L, 1),
            (uint8_t)luaL_checkinteger(L, 2),
            (uint8_t)luaL_checkinteger(L, 3));
    } else {
        fabgl_set_brush_color_idx((int)luaL_checkinteger(L, 1));
    }
    return 0;
}

/* vga.setPenWidth(w) */
static int lvga_setPenWidth(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_set_pen_width((int)luaL_checkinteger(L, 1));
    return 0;
}

/* vga.setScrollingRegion(x1, y1, x2, y2) */
static int lvga_setScrollingRegion(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_set_scrolling_region(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

/* ---------- drawing ---------- */

static int lvga_clear(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_clear();
    return 0;
}

static int lvga_setPixel(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_set_pixel((int)luaL_checkinteger(L, 1),
                    (int)luaL_checkinteger(L, 2));
    return 0;
}

/* vga.getPixel(x, y) -> r, g, b */
static int lvga_getPixel(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    uint8_t r, g, b;
    fabgl_get_pixel((int)luaL_checkinteger(L, 1),
                    (int)luaL_checkinteger(L, 2),
                    &r, &g, &b);
    lua_pushinteger(L, r);
    lua_pushinteger(L, g);
    lua_pushinteger(L, b);
    return 3;
}

static int lvga_moveTo(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_move_to((int)luaL_checkinteger(L, 1),
                  (int)luaL_checkinteger(L, 2));
    return 0;
}

static int lvga_lineTo(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_line_to((int)luaL_checkinteger(L, 1),
                  (int)luaL_checkinteger(L, 2));
    return 0;
}

static int lvga_drawLine(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_draw_line(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

static int lvga_drawRect(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_draw_rect(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

static int lvga_fillRect(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_fill_rect(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

static int lvga_drawEllipse(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_draw_ellipse(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

static int lvga_fillEllipse(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_fill_ellipse(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

/* ---------- text ---------- */

/*
 * vga.selectFont(font_id)
 *   font_id: one of vga.FONT_4x6 .. vga.FONT_8x16
 */
static int lvga_selectFont(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_select_font((int)luaL_checkinteger(L, 1));
    return 0;
}

/* vga.drawText(x, y, text [, wrap]) */
static int lvga_drawText(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_draw_text(
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        luaL_checkstring(L, 3),
        (bool)lua_toboolean(L, 4));
    return 0;
}

/* vga.textWidth(text) -> integer */
static int lvga_textWidth(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    lua_pushinteger(L, fabgl_text_width(luaL_checkstring(L, 1)));
    return 1;
}

/* ---------- region / copy ---------- */

static int lvga_scroll(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_scroll((int)luaL_checkinteger(L, 1),
                 (int)luaL_checkinteger(L, 2));
    return 0;
}

static int lvga_copyRect(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_copy_rect(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
        (int)luaL_checkinteger(L, 5), (int)luaL_checkinteger(L, 6));
    return 0;
}

/* ---------- sync ---------- */

static int lvga_waitCompletion(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    bool vsync = lua_isnoneornil(L, 1) ? true : (bool)lua_toboolean(L, 1);
    fabgl_wait_completion(vsync);
    return 0;
}

static int lvga_swapBuffers(lua_State *L)
{
    if (!fabgl_vga_started()) luaL_error(L, "vga not started");
    fabgl_swap_buffers();
    return 0;
}

/* ---------- module registration ---------- */

static const LUA_REG_TYPE vga_map[] = {
    /* Lifecycle */
    { LSTRKEY("begin"),              LFUNCVAL(lvga_begin)              },
    { LSTRKEY("setResolution"),      LFUNCVAL(lvga_setResolution)      },
    { LSTRKEY("done"),               LFUNCVAL(lvga_done)               },
    /* Canvas dimensions */
    { LSTRKEY("width"),              LFUNCVAL(lvga_width)              },
    { LSTRKEY("height"),             LFUNCVAL(lvga_height)             },
    /* Paint state */
    { LSTRKEY("setPenColor"),        LFUNCVAL(lvga_setPenColor)        },
    { LSTRKEY("setBrushColor"),      LFUNCVAL(lvga_setBrushColor)      },
    { LSTRKEY("setPenWidth"),        LFUNCVAL(lvga_setPenWidth)        },
    { LSTRKEY("setScrollingRegion"), LFUNCVAL(lvga_setScrollingRegion) },
    /* Drawing */
    { LSTRKEY("clear"),              LFUNCVAL(lvga_clear)              },
    { LSTRKEY("setPixel"),           LFUNCVAL(lvga_setPixel)           },
    { LSTRKEY("getPixel"),           LFUNCVAL(lvga_getPixel)           },
    { LSTRKEY("moveTo"),             LFUNCVAL(lvga_moveTo)             },
    { LSTRKEY("lineTo"),             LFUNCVAL(lvga_lineTo)             },
    { LSTRKEY("drawLine"),           LFUNCVAL(lvga_drawLine)           },
    { LSTRKEY("drawRect"),           LFUNCVAL(lvga_drawRect)           },
    { LSTRKEY("fillRect"),           LFUNCVAL(lvga_fillRect)           },
    { LSTRKEY("drawEllipse"),        LFUNCVAL(lvga_drawEllipse)        },
    { LSTRKEY("fillEllipse"),        LFUNCVAL(lvga_fillEllipse)        },
    /* Text */
    { LSTRKEY("selectFont"),         LFUNCVAL(lvga_selectFont)         },
    { LSTRKEY("drawText"),           LFUNCVAL(lvga_drawText)           },
    { LSTRKEY("textWidth"),          LFUNCVAL(lvga_textWidth)          },
    /* Region / copy */
    { LSTRKEY("scroll"),             LFUNCVAL(lvga_scroll)             },
    { LSTRKEY("copyRect"),           LFUNCVAL(lvga_copyRect)           },
    /* Sync */
    { LSTRKEY("waitCompletion"),     LFUNCVAL(lvga_waitCompletion)     },
    { LSTRKEY("swapBuffers"),        LFUNCVAL(lvga_swapBuffers)        },

    /* Colour constants (fabgl::Color enum, 0-15) */
    { LSTRKEY("Black"),         LINTVAL(0)  },
    { LSTRKEY("Red"),           LINTVAL(1)  },
    { LSTRKEY("Green"),         LINTVAL(2)  },
    { LSTRKEY("Yellow"),        LINTVAL(3)  },
    { LSTRKEY("Blue"),          LINTVAL(4)  },
    { LSTRKEY("Magenta"),       LINTVAL(5)  },
    { LSTRKEY("Cyan"),          LINTVAL(6)  },
    { LSTRKEY("White"),         LINTVAL(7)  },
    { LSTRKEY("BrightBlack"),   LINTVAL(8)  },
    { LSTRKEY("BrightRed"),     LINTVAL(9)  },
    { LSTRKEY("BrightGreen"),   LINTVAL(10) },
    { LSTRKEY("BrightYellow"),  LINTVAL(11) },
    { LSTRKEY("BrightBlue"),    LINTVAL(12) },
    { LSTRKEY("BrightMagenta"), LINTVAL(13) },
    { LSTRKEY("BrightCyan"),    LINTVAL(14) },
    { LSTRKEY("BrightWhite"),   LINTVAL(15) },

    /* Font id constants for selectFont() */
    { LSTRKEY("FONT_4x6"),  LINTVAL(0) },
    { LSTRKEY("FONT_6x8"),  LINTVAL(1) },
    { LSTRKEY("FONT_8x8"),  LINTVAL(2) },
    { LSTRKEY("FONT_8x9"),  LINTVAL(3) },
    { LSTRKEY("FONT_8x14"), LINTVAL(4) },
    { LSTRKEY("FONT_8x16"), LINTVAL(5) },

    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_vga(lua_State *L)
{
    (void)L;
    return 0;
}

MODULE_REGISTER_ROM(VGA, vga, vga_map, luaopen_vga, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_VGA */
