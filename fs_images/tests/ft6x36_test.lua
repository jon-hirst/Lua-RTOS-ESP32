-- FT6x36 capacitive touch controller test
--
-- Exercises every method exposed by the ft6x36 Lua module:
--   setup, positions, gesture, rotation, threshold, monitortime,
--   activeperiod, monitorperiod, intmode, powermode,
--   libversion, fwversion, vendorid, panelid
--
-- Hardware: FT6x36 on I2C1 (SDA=GPIO39, SCL=GPIO40), 240x240 panel

-- -------------------------------------------------------------------------
-- Helpers
-- -------------------------------------------------------------------------

local function sleep(ms)
    tmr.sleepms(ms)
end

local function banner(title)
    print("")
    print("--- " .. title .. " ---")
end

local function check(label, got, expected)
    if got == expected then
        print(string.format("  OK  %s = %s", label, tostring(got)))
    else
        print(string.format("  FAIL %s: expected %s, got %s",
              label, tostring(expected), tostring(got)))
    end
end

local function checkrange(label, val, lo, hi)
    if val >= lo and val <= hi then
        print(string.format("  OK  %s = %d (in [%d, %d])", label, val, lo, hi))
    else
        print(string.format("  FAIL %s = %d (expected [%d, %d])", label, val, lo, hi))
    end
end

-- -------------------------------------------------------------------------
-- Setup
-- -------------------------------------------------------------------------

banner("Setup")
print("Initialising FT6x36 on I2C1 (SDA=GPIO39, SCL=GPIO40), 240x240...")
local dev = ft6x36.setup(240, 240)
print("OK")

-- -------------------------------------------------------------------------
-- Module constants
-- -------------------------------------------------------------------------

banner("Module Constants")
print(string.format("  PORTRAIT           = %d", ft6x36.PORTRAIT))
print(string.format("  LANDSCAPE          = %d", ft6x36.LANDSCAPE))
print(string.format("  PORTRAIT_INVERTED  = %d", ft6x36.PORTRAIT_INVERTED))
print(string.format("  LANDSCAPE_INVERTED = %d", ft6x36.LANDSCAPE_INVERTED))
print(string.format("  GESTURE_NONE       = %d", ft6x36.GESTURE_NONE))
print(string.format("  GESTURE_MOVE_UP    = %d", ft6x36.GESTURE_MOVE_UP))
print(string.format("  GESTURE_MOVE_LEFT  = %d", ft6x36.GESTURE_MOVE_LEFT))
print(string.format("  GESTURE_MOVE_DOWN  = %d", ft6x36.GESTURE_MOVE_DOWN))
print(string.format("  GESTURE_MOVE_RIGHT = %d", ft6x36.GESTURE_MOVE_RIGHT))
print(string.format("  GESTURE_ZOOM_IN    = %d", ft6x36.GESTURE_ZOOM_IN))
print(string.format("  GESTURE_ZOOM_OUT   = %d", ft6x36.GESTURE_ZOOM_OUT))
print(string.format("  POLLING_MODE       = %d", ft6x36.POLLING_MODE))
print(string.format("  TRIGGER_MODE       = %d", ft6x36.TRIGGER_MODE))

-- -------------------------------------------------------------------------
-- Chip identification (read-only registers)
-- -------------------------------------------------------------------------

banner("Chip Identification")
local fw  = dev:fwversion()
local lib = dev:libversion()
local vid = dev:vendorid()
local pid = dev:panelid()
print(string.format("  firmware version : 0x%02X", fw))
print(string.format("  library version  : 0x%04X", lib))
print(string.format("  vendor ID        : 0x%02X", vid))
print(string.format("  panel ID         : 0x%02X  (FocalTech = 0x11)", pid))

-- -------------------------------------------------------------------------
-- Rotation: cycle through all four settings and read back
-- -------------------------------------------------------------------------

banner("Rotation: get/set all four orientations")
check("default rotation", dev:rotation(), ft6x36.PORTRAIT)

for _, r in ipairs({
    ft6x36.LANDSCAPE,
    ft6x36.PORTRAIT_INVERTED,
    ft6x36.LANDSCAPE_INVERTED,
    ft6x36.PORTRAIT,
}) do
    dev:rotation(r)
    check("rotation = " .. tostring(r), dev:rotation(), r)
end

-- -------------------------------------------------------------------------
-- Threshold
-- -------------------------------------------------------------------------

banner("Threshold (TH_GROUP)")
local orig_thresh = dev:threshold()
print(string.format("  default threshold = %d", orig_thresh))
checkrange("threshold in plausible range", orig_thresh, 0, 255)

-- Write a different value and read back
local new_thresh = (orig_thresh == 22) and 30 or 22
dev:threshold(new_thresh)
check("threshold write/read", dev:threshold(), new_thresh)

-- Restore original
dev:threshold(orig_thresh)
check("threshold restored", dev:threshold(), orig_thresh)

-- -------------------------------------------------------------------------
-- Monitor time
-- -------------------------------------------------------------------------

banner("Monitor Time (TIMEENTERMONITOR)")
local orig_mt = dev:monitortime()
print(string.format("  default monitor time = %d s", orig_mt))
local new_mt = (orig_mt == 10) and 5 or 10
dev:monitortime(new_mt)
check("monitortime write/read", dev:monitortime(), new_mt)
dev:monitortime(orig_mt)
check("monitortime restored", dev:monitortime(), orig_mt)

-- -------------------------------------------------------------------------
-- Active period
-- -------------------------------------------------------------------------

banner("Active Period (PERIODACTIVE)")
local orig_ap = dev:activeperiod()
print(string.format("  default active period = %d ms", orig_ap))
local new_ap = (orig_ap == 14) and 10 or 14
dev:activeperiod(new_ap)
check("activeperiod write/read", dev:activeperiod(), new_ap)
dev:activeperiod(orig_ap)
check("activeperiod restored", dev:activeperiod(), orig_ap)

-- -------------------------------------------------------------------------
-- Monitor period
-- -------------------------------------------------------------------------

banner("Monitor Period (PERIODMONITOR)")
local orig_mp = dev:monitorperiod()
print(string.format("  default monitor period = %d ms", orig_mp))
local new_mp = (orig_mp == 40) and 30 or 40
dev:monitorperiod(new_mp)
check("monitorperiod write/read", dev:monitorperiod(), new_mp)
dev:monitorperiod(orig_mp)
check("monitorperiod restored", dev:monitorperiod(), orig_mp)

-- -------------------------------------------------------------------------
-- Interrupt mode
-- -------------------------------------------------------------------------

banner("Interrupt Mode (G_MODE)")
local orig_im = dev:intmode()
print(string.format("  default int mode = %d  (0=polling, 1=trigger)", orig_im))

dev:intmode(ft6x36.TRIGGER_MODE)
check("intmode set to TRIGGER", dev:intmode(), ft6x36.TRIGGER_MODE)
dev:intmode(ft6x36.POLLING_MODE)
check("intmode set to POLLING", dev:intmode(), ft6x36.POLLING_MODE)

dev:intmode(orig_im)
check("intmode restored", dev:intmode(), orig_im)

-- -------------------------------------------------------------------------
-- Power mode
-- -------------------------------------------------------------------------

banner("Power Mode (PWR_MODE)")
local pm = dev:powermode()
print(string.format("  current power mode = %d", pm))
-- Read back only — do not change power mode to avoid disrupting the touch IC

-- -------------------------------------------------------------------------
-- Gesture (polled, no touch)
-- -------------------------------------------------------------------------

banner("Gesture (no touch expected)")
local g = dev:gesture()
print(string.format("  gesture = %d  (0 = none)", g))
if g == ft6x36.GESTURE_NONE then
    print("  OK  no gesture when not touching")
else
    print(string.format("  INFO unexpected gesture %d (is the screen being touched?)", g))
end

-- -------------------------------------------------------------------------
-- Positions (polled, no touch)
-- -------------------------------------------------------------------------

banner("Positions (no touch expected)")
local n = dev:positions()
print(string.format("  touch count = %d", n))
if n == 0 then
    print("  OK  no touches when screen is not touched")
else
    print(string.format("  INFO %d touch(es) reported (is the screen being touched?)", n))
end

-- -------------------------------------------------------------------------
-- Live touch poll: 5 seconds, report any touches
-- -------------------------------------------------------------------------

banner("Live Touch Poll (5 seconds — touch the screen now)")
local deadline = 50  -- 50 × 100 ms = 5 s
local touches_seen = 0

for i = 1, deadline do
    local count, x1, y1, w1, a1, x2, y2, w2, a2 = dev:positions()
    if count > 0 then
        touches_seen = touches_seen + 1
        if count == 1 then
            print(string.format("  [%d] 1 touch: (%4d,%4d) w=%3d area=%d",
                  i, x1, y1, w1, a1))
        else
            print(string.format("  [%d] 2 touch: P1(%4d,%4d) w=%3d a=%d  P2(%4d,%4d) w=%3d a=%d",
                  i, x1, y1, w1, a1, x2, y2, w2, a2))
        end
        -- Also print gesture while touching
        local gest = dev:gesture()
        if gest ~= ft6x36.GESTURE_NONE then
            print(string.format("       gesture = %d", gest))
        end
    end
    sleep(100)
end

print(string.format("  Total touch events: %d", touches_seen))

-- -------------------------------------------------------------------------
-- Rotation: verify coordinate transformation with portrait inverted
-- -------------------------------------------------------------------------

banner("Rotation Transform Check (PORTRAIT_INVERTED)")
dev:rotation(ft6x36.PORTRAIT_INVERTED)
check("rotation is PORTRAIT_INVERTED", dev:rotation(), ft6x36.PORTRAIT_INVERTED)
print("  (coordinates will be mirrored; touch test not repeated)")

-- Restore portrait
dev:rotation(ft6x36.PORTRAIT)
check("rotation restored to PORTRAIT", dev:rotation(), ft6x36.PORTRAIT)

-- -------------------------------------------------------------------------
-- Done
-- -------------------------------------------------------------------------

banner("Summary")
print("All tests complete.")
print(string.format("Final state: rotation=%d  threshold=%d  intmode=%d",
      dev:rotation(), dev:threshold(), dev:intmode()))
