--[[
  ST7789 240x240 262K-color LCD comprehensive test
  Exercises every feature exposed by sys/drivers/st7789.c through the
  gdisplay Lua module.

  Usage:
    dofile("/sd/st7789_test.lua")   -- from SD card
    dofile("st7789_test.lua")       -- from SPIFFS / embedded FS
--]]

local DELAY_S = 2          -- seconds to pause between demos
local W, H                 -- filled in after init

-- ── helpers ────────────────────────────────────────────────────────────────

local function pause(s)
  tmr.delay(s or DELAY_S)
end

local function banner(title)
  print("[ST7789] " .. title)
  gdisplay.clear(gdisplay.BLACK)
  gdisplay.setfont(gdisplay.FONT_DEFAULT)
  gdisplay.setcolor(gdisplay.CYAN)
  -- header bar
  gdisplay.rect(0, 0, W, 18, gdisplay.DARKCYAN, gdisplay.DARKCYAN)
  gdisplay.settransp(true)
  gdisplay.write(gdisplay.CENTER, 2, title)
  gdisplay.settransp(false)
  gdisplay.setcolor(gdisplay.WHITE)
end

local function rndcolor()
  return gdisplay.hsb2rgb(math.random(360), 1, 1)
end

-- ── 1. Initialise the display ───────────────────────────────────────────────
print("[ST7789] Initialising ST7789 240x240 262K-colour LCD ...")
math.randomseed(os.time and os.time() or 42)

gdisplay.init(gdisplay.ST7789, gdisplay.PORTRAIT)
W, H = gdisplay.getscreensize()
print(string.format("[ST7789] Screen size: %d x %d", W, H))

-- ── 2. Display-on / display-off ────────────────────────────────────────────
print("[ST7789] Test: display OFF / ON")
gdisplay.clear(gdisplay.BLUE)
gdisplay.setfont(gdisplay.FONT_DEFAULT)
gdisplay.setcolor(gdisplay.WHITE)
gdisplay.write(gdisplay.CENTER, gdisplay.CENTER, "Display ON/OFF test")
pause(1)
gdisplay.off()        -- ST7789 DISPOFF command
tmr.delay(1)
gdisplay.on()         -- ST7789 DISPON command
pause(1)

-- ── 3. Colour-fill / clear with explicit colour ─────────────────────────────
print("[ST7789] Test: clear with 262K colours")
local solid_colors = {
  gdisplay.RED,  gdisplay.GREEN,  gdisplay.BLUE,
  gdisplay.CYAN, gdisplay.MAGENTA, gdisplay.YELLOW,
  gdisplay.WHITE, gdisplay.BLACK
}
for _, c in ipairs(solid_colors) do
  gdisplay.clear(c)
  tmr.delayms(300)
end

-- ── 4. Colour model helpers: rgb() and hsb2rgb() ───────────────────────────
print("[ST7789] Test: rgb() colour constructor")
banner("rgb() COLOURS")
-- Draw a 6x6 grid of colours built with gdisplay.rgb()
local step_r = 255 // 5
local step_g = 255 // 5
local step_b = 255 // 5
local cell_w = W // 6
local cell_h = (H - 20) // 6
for ri = 0, 5 do
  for gi = 0, 5 do
    local r = ri * step_r
    local g = gi * step_g
    local b = (ri + gi) % 6 * step_b
    local col = gdisplay.rgb(r, g, b)
    local x = ri * cell_w
    local y = 20 + gi * cell_h
    gdisplay.rect(x, y, cell_w, cell_h, col, col)
  end
end
pause()

-- ── 5. Rainbow gradient using hsb2rgb() ────────────────────────────────────
print("[ST7789] Test: hsb2rgb() rainbow gradient")
banner("HSB RAINBOW GRADIENT")
local rows = H - 20
for i = 0, rows - 1 do
  local hue = (i / rows) * 360
  local col = gdisplay.hsb2rgb(hue, 1, 1)
  gdisplay.line(0, 20 + i, W - 1, 20 + i, col)
end
pause()

-- ── 6. Pixel operations: putpixel / getpixel ───────────────────────────────
print("[ST7789] Test: putpixel / getpixel")
banner("PIXEL OPS")
-- scatter random pixels
for _ = 1, 2000 do
  local px = math.random(0, W - 1)
  local py = math.random(20, H - 1)
  gdisplay.putpixel(px, py, rndcolor())
end
-- verify a specific pixel round-trip
gdisplay.putpixel(120, 160, gdisplay.RED)
local pval = gdisplay.getpixel(120, 160)
if pval == gdisplay.RED then
  print("[ST7789]   getpixel round-trip: PASS")
else
  print("[ST7789]   getpixel round-trip: value=" .. tostring(pval))
end
pause()

-- ── 7. Lines ────────────────────────────────────────────────────────────────
print("[ST7789] Test: lines")
banner("LINES")
for _ = 1, 80 do
  gdisplay.line(
    math.random(0, W-1), math.random(20, H-1),
    math.random(0, W-1), math.random(20, H-1),
    rndcolor()
  )
end
pause()

-- ── 8. Line-by-angle ───────────────────────────────────────────────────────
print("[ST7789] Test: linebyangle")
banner("LINE BY ANGLE")
local cx, cy = W // 2, H // 2
for ang = 0, 359, 6 do
  gdisplay.linebyangle(cx, cy, 100, ang, rndcolor())
end
pause()

-- ── 9. Rectangles (outline + filled) ───────────────────────────────────────
print("[ST7789] Test: rect outline + filled")
banner("RECTANGLES")
-- outline
for _ = 1, 20 do
  local rx = math.random(0, W - 40)
  local ry = math.random(20, H - 40)
  gdisplay.rect(rx, ry, math.random(10, W - rx), math.random(10, H - ry), rndcolor())
end
pause(1)
gdisplay.clear(gdisplay.BLACK)
-- filled
for _ = 1, 20 do
  local rx = math.random(0, W - 40)
  local ry = math.random(20, H - 40)
  gdisplay.rect(rx, ry, math.random(10, W - rx), math.random(10, H - ry), rndcolor(), rndcolor())
end
pause()

-- ── 10. Rounded rectangles ─────────────────────────────────────────────────
print("[ST7789] Test: roundrect")
banner("ROUNDED RECTS")
for _ = 1, 20 do
  local rx = math.random(0, W - 60)
  local ry = math.random(20, H - 60)
  local rw = math.random(20, W - rx)
  local rh = math.random(20, H - ry)
  local rad = math.random(2, math.min(rw, rh) // 2)
  gdisplay.roundrect(rx, ry, rw, rh, rad, rndcolor(), rndcolor())
end
pause()

-- ── 11. Circles (outline + filled) ─────────────────────────────────────────
print("[ST7789] Test: circle outline + filled")
banner("CIRCLES")
for _ = 1, 30 do
  local x = math.random(10, W - 10)
  local y = math.random(30, H - 10)
  local r = math.random(5, 40)
  gdisplay.circle(x, y, r, rndcolor(), rndcolor())
end
pause()

-- ── 12. Ellipses ────────────────────────────────────────────────────────────
print("[ST7789] Test: ellipse")
banner("ELLIPSES")
for _ = 1, 20 do
  local x  = math.random(20, W - 20)
  local y  = math.random(30, H - 20)
  local rx = math.random(5, 50)
  local ry = math.random(5, 50)
  gdisplay.ellipse(x, y, rx, ry, rndcolor(), rndcolor())
end
pause()

-- ── 13. Arcs ────────────────────────────────────────────────────────────────
print("[ST7789] Test: arc")
banner("ARCS")
for _ = 1, 15 do
  local x  = math.random(30, W - 30)
  local y  = math.random(40, H - 30)
  local r  = math.random(15, 50)
  local th = math.random(2, 10)
  local a1 = math.random(0, 300)
  local a2 = a1 + math.random(30, 180)
  gdisplay.arc(x, y, r, th, a1, a2, rndcolor(), rndcolor())
end
pause()

-- ── 14. Triangles (outline + filled) ───────────────────────────────────────
print("[ST7789] Test: triangle outline + filled")
banner("TRIANGLES")
for _ = 1, 20 do
  gdisplay.triangle(
    math.random(0, W-1), math.random(20, H-1),
    math.random(0, W-1), math.random(20, H-1),
    math.random(0, W-1), math.random(20, H-1),
    rndcolor(), rndcolor()
  )
end
pause()

-- ── 15. Polygons ────────────────────────────────────────────────────────────
print("[ST7789] Test: poly")
banner("POLYGONS")
for sides = 3, 8 do
  local x   = W // 2
  local y   = H // 2
  local r   = 50
  local rot = math.random(0, 359)
  gdisplay.poly(x, y, sides, r, rot, rndcolor(), rndcolor())
  tmr.delayms(400)
end
pause()

-- ── 16. Stars ───────────────────────────────────────────────────────────────
print("[ST7789] Test: star")
banner("STARS")
for _ = 1, 10 do
  local x    = math.random(40, W - 40)
  local y    = math.random(50, H - 40)
  local r    = math.random(15, 50)
  local fact = math.random(3, 8)
  gdisplay.star(x, y, r, fact, rndcolor(), rndcolor())
end
pause()

-- ── 17. Clip window ─────────────────────────────────────────────────────────
print("[ST7789] Test: clip window")
banner("CLIP WINDOW")
-- Draw outside the clip region first, then enable clip
gdisplay.setclipwin(40, 60, W - 40, H - 60)
for _ = 1, 60 do
  -- deliberately uses full-screen coords — clipping should confine drawing
  gdisplay.line(
    math.random(0, W-1), math.random(0, H-1),
    math.random(0, W-1), math.random(0, H-1),
    rndcolor()
  )
end
-- Show the clip boundary in white
gdisplay.resetclipwin()
gdisplay.rect(40, 60, W - 80, H - 120, gdisplay.WHITE)
pause()

-- ── 18. Text and fonts ──────────────────────────────────────────────────────
print("[ST7789] Test: fonts and text")
local fonts = {
  gdisplay.FONT_DEFAULT,
  gdisplay.FONT_DEJAVU18,
  gdisplay.FONT_DEJAVU24,
  gdisplay.FONT_UBUNTU16,
  gdisplay.FONT_COMIC24,
  gdisplay.FONT_TOONEY32,
  gdisplay.FONT_MINYA24,
  gdisplay.FONT_LCD,
}
gdisplay.clear(gdisplay.BLACK)
gdisplay.setcolor(gdisplay.YELLOW)
gdisplay.setfont(gdisplay.FONT_DEFAULT)
gdisplay.write(gdisplay.CENTER, 2, "FONT DEMO")
local ty = 20
for _, fnt in ipairs(fonts) do
  gdisplay.setfont(fnt)
  local fw, fh = gdisplay.getfontsize()
  gdisplay.setcolor(rndcolor())
  gdisplay.write(0, ty, "ST7789 262K")
  ty = ty + fh + 2
  if ty > H - fh then break end
end
pause()

-- ── 19. Text rotation ───────────────────────────────────────────────────────
print("[ST7789] Test: text rotation")
banner("TEXT ROTATION")
gdisplay.setfont(gdisplay.FONT_UBUNTU16)
for rot = 0, 359, 45 do
  gdisplay.setrot(rot)
  gdisplay.setcolor(rndcolor())
  gdisplay.write(gdisplay.CENTER, gdisplay.CENTER, "Rotated!")
  tmr.delayms(500)
end
gdisplay.setrot(0)
pause()

-- ── 20. Transparency ────────────────────────────────────────────────────────
print("[ST7789] Test: transparency")
banner("TRANSPARENCY")
-- background of coloured rectangles
for i = 0, 5 do
  gdisplay.rect(i * 40, 20, 40, H - 20, rndcolor(), rndcolor())
end
-- text drawn transparently on top
gdisplay.setfont(gdisplay.FONT_DEJAVU24)
gdisplay.setcolor(gdisplay.WHITE)
gdisplay.settransp(true)
gdisplay.write(gdisplay.CENTER, gdisplay.CENTER, "TRANSPARENT")
gdisplay.settransp(false)
pause()

-- ── 21. Colour-depth stress: full 18-bit palette sweep ─────────────────────
print("[ST7789] Test: 262K colour sweep (18-bit)")
banner("262K COLOUR SWEEP")
-- Sweep hue across the whole display height; vary saturation left-to-right
local stripe_h = (H - 20) // 36
for hue_step = 0, 35 do
  local hue = hue_step * 10
  for col = 0, W - 1 do
    local sat = col / (W - 1)
    local c = gdisplay.hsb2rgb(hue, sat, 1)
    gdisplay.line(col, 20 + hue_step * stripe_h,
                  col, 20 + (hue_step + 1) * stripe_h - 1, c)
  end
end
pause()

-- ── 22. Invert ──────────────────────────────────────────────────────────────
print("[ST7789] Test: display invert (INVON / INVOFF)")
-- Leave the colour sweep visible and toggle invert
gdisplay.invert(true)    -- ST7789_INVON
tmr.delay(1)
gdisplay.invert(false)   -- ST7789_INVOFF
tmr.delay(1)
gdisplay.invert(true)
tmr.delay(1)
gdisplay.invert(false)
pause(1)

-- ── 23. Orientation changes ─────────────────────────────────────────────────
print("[ST7789] Test: all four orientations")
local orientations = {
  { gdisplay.PORTRAIT,       "PORTRAIT 240x240"   },
  { gdisplay.LANDSCAPE,      "LANDSCAPE 240x240"  },
  { gdisplay.PORTRAIT_FLIP,  "PORTRAIT FLIP"      },
  { gdisplay.LANDSCAPE_FLIP, "LANDSCAPE FLIP"     },
}
for _, ot in ipairs(orientations) do
  gdisplay.setorient(ot[1])
  W, H = gdisplay.getscreensize()
  gdisplay.clear(gdisplay.NAVY)
  gdisplay.setfont(gdisplay.FONT_DEFAULT)
  gdisplay.setcolor(gdisplay.WHITE)
  gdisplay.write(gdisplay.CENTER, gdisplay.CENTER, ot[2])
  gdisplay.circle(W // 2, H // 2, 40, gdisplay.YELLOW)
  pause(1)
end

-- Restore portrait
gdisplay.setorient(gdisplay.PORTRAIT)
W, H = gdisplay.getscreensize()

-- ── 24. Screen-size query ───────────────────────────────────────────────────
print("[ST7789] Test: getscreensize")
local sw, sh = gdisplay.getscreensize()
assert(sw == 240 and sh == 240,
  string.format("Expected 240x240, got %dx%d", sw, sh))
print(string.format("[ST7789]   getscreensize: %dx%d  PASS", sw, sh))

-- ── 25. Background / foreground / stroke colour setters ────────────────────
print("[ST7789] Test: setforeground / setbackground / setstroke")
gdisplay.clear(gdisplay.BLACK)
gdisplay.setforeground(gdisplay.GREEN)
gdisplay.setbackground(gdisplay.DARKGREEN)
gdisplay.setstroke(gdisplay.YELLOW)
gdisplay.setfont(gdisplay.FONT_UBUNTU16)
-- write uses foreground/background defaults
gdisplay.write(20, 60, "Foreground GREEN")
gdisplay.write(20, 90, "Background DKGRN")
-- rect uses setstroke default for outline
gdisplay.rect(20, 120, 180, 40)
pause()

-- ── 26. Angle offset ────────────────────────────────────────────────────────
print("[ST7789] Test: angle offset")
banner("ANGLE OFFSET")
gdisplay.setangleoffset(0)
local base_off = gdisplay.getangleoffset()
print(string.format("[ST7789]   default angle offset = %g", base_off))
-- Draw a spoke at 0° (should point right)
gdisplay.linebyangle(W//2, H//2, 80, 0, gdisplay.WHITE)
gdisplay.setangleoffset(90)
gdisplay.linebyangle(W//2, H//2, 80, 0, gdisplay.RED)   -- now points up
gdisplay.setangleoffset(0)
pause()

-- ── 27. Fixed / wrap flags ──────────────────────────────────────────────────
print("[ST7789] Test: setwrap / setfixed")
banner("WRAP & FIXED")
gdisplay.setwrap(true)
gdisplay.setfixed(false)
gdisplay.setfont(gdisplay.FONT_DEFAULT)
gdisplay.setcolor(gdisplay.WHITE)
gdisplay.write(0, H//2, "This is a long string that should wrap to the next line automatically.")
pause()
gdisplay.setwrap(false)
gdisplay.setfixed(false)

-- ── 28. QR code ─────────────────────────────────────────────────────────────
print("[ST7789] Test: qrcode")
banner("QR CODE")
gdisplay.setforeground(gdisplay.BLACK)
gdisplay.setbackground(gdisplay.WHITE)
-- fill white background for QR readability
gdisplay.rect(20, 30, W - 40, W - 40, gdisplay.WHITE, gdisplay.WHITE)
gdisplay.qrcode(20, 30, "https://whitecatboard.org", gdisplay.ECC_MEDIUM, 2)
pause()

-- ── Done ────────────────────────────────────────────────────────────────────
gdisplay.clear(gdisplay.BLACK)
gdisplay.setfont(gdisplay.FONT_UBUNTU16)
gdisplay.setcolor(gdisplay.GREEN)
gdisplay.write(gdisplay.CENTER, gdisplay.CENTER - 20, "ST7789 TEST DONE")
gdisplay.setfont(gdisplay.FONT_DEFAULT)
gdisplay.setcolor(gdisplay.LIGHTGREY)
gdisplay.write(gdisplay.CENTER, gdisplay.CENTER + 10, "240x240  262K colours")
print("[ST7789] All tests complete.")
