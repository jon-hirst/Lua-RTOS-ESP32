-- DRV2605 haptic motor driver test
--
-- Exercises every method exposed by the drv2605 Lua module:
--   setup, status, mode, library, setwaveform, setpause, getslot,
--   rtpvalue, play, stop
--
-- Effect IDs are from the TS2200A library (Table 3 of the DRV2605
-- datasheet).  You should feel a distinct vibration for each test.

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
        print(string.format("  OK  %s = %d", label, got))
    else
        print(string.format("  FAIL %s: expected %d, got %d", label, expected, got))
    end
end

-- -------------------------------------------------------------------------
-- Setup
-- -------------------------------------------------------------------------

print("Power on")
pmu = axp2101.setup()
pmu:bldo2_enable()

banner("Setup")
print("Initialising DRV2605 on I2C0 (SDA=GPIO10, SCL=GPIO11)...")
local dev = drv2605.setup()
print("OK")

-- -------------------------------------------------------------------------
-- Status register
-- -------------------------------------------------------------------------

banner("Status Register")
local status    = dev:status()
local device_id = (status >> 5) & 0x07
print(string.format("  STATUS = 0x%02X", status))
print(string.format("  Device ID = %d  (3 = DRV2605, 7 = DRV2605L)", device_id))
if device_id == 3 or device_id == 7 then
    print("  Device identified OK")
else
    print("  WARNING: unexpected device ID")
end

-- -------------------------------------------------------------------------
-- Mode and library read-back
-- -------------------------------------------------------------------------

banner("Default Mode and Library")
check("mode",    dev:mode(),    drv2605.MODE_INTTRIG)
check("library", dev:library(), drv2605.LIBRARY_TS2200A)

-- -------------------------------------------------------------------------
-- Single-effect tests  (TS2200A library)
-- -------------------------------------------------------------------------

-- A small selection of effects that feel clearly different on an ERM.
-- Effect 0 in any slot terminates the sequence.
local effects = {
    {  1, "Strong Click 100%"             },
    {  4, "Sharp Click 100%"              },
    { 10, "Double Click 100%"             },
    { 14, "Strong Buzz 100%"              },
    { 47, "Pulsing Medium 1"              },
    { 56, "Transition Ramp Down Long Smooth 1" },
}

banner("Single-Effect Playback (TS2200A, slot 0)")
for _, e in ipairs(effects) do
    local id, name = e[1], e[2]
    print(string.format("  Effect %3d: %s", id, name))
    dev:setwaveform(0, id)
    dev:setwaveform(1, 0)   -- terminate sequence at slot 1
    dev:play()
    sleep(1000)
end

-- -------------------------------------------------------------------------
-- Sequence test: multiple effects with a timed pause between them
-- -------------------------------------------------------------------------

banner("Sequence: click → 0.5 s pause → buzz → stop")

dev:setwaveform(0,  1)   -- strong click
dev:setpause   (1, 50)   -- 50 centiseconds = 0.50 s
dev:setwaveform(2, 14)   -- strong buzz
dev:setwaveform(3,  0)   -- terminate

dev:play()
sleep(2000)

-- Verify the slot contents read back correctly
banner("Slot Read-back Verification")
local slot0 = dev:getslot(0)
local slot1 = dev:getslot(1)
local slot2 = dev:getslot(2)
local slot3 = dev:getslot(3)

check("slot 0 (effect 1)",     slot0, 1)
check("slot 1 (pause flag)",   slot1 & 0x80, 0x80)
check("slot 1 (50 cs pause)",  slot1 & 0x7F, 50)
check("slot 2 (effect 14)",    slot2, 14)
check("slot 3 (terminator 0)", slot3, 0)

-- -------------------------------------------------------------------------
-- Library switch test
-- -------------------------------------------------------------------------

banner("Library Switch: TS2200A vs TS2200B, effect 1")

dev:setwaveform(0, 1)
dev:setwaveform(1, 0)

print("  Playing with TS2200A...")
dev:library(drv2605.LIBRARY_TS2200A)
dev:play()
sleep(1000)

print("  Playing with TS2200B...")
dev:library(drv2605.LIBRARY_TS2200B)
dev:play()
sleep(1000)

-- Restore default library
dev:library(drv2605.LIBRARY_TS2200A)
check("library restored", dev:library(), drv2605.LIBRARY_TS2200A)

-- -------------------------------------------------------------------------
-- Mode switching test
-- -------------------------------------------------------------------------

banner("Mode Switching")

-- Switch to REALTIME and back, verifying the register changes
dev:mode(drv2605.MODE_REALTIME)
check("mode set to REALTIME", dev:mode(), drv2605.MODE_REALTIME)
dev:mode(drv2605.MODE_INTTRIG)
check("mode restored to INTTRIG", dev:mode(), drv2605.MODE_INTTRIG)

-- -------------------------------------------------------------------------
-- Real-time playback (RTP) test
-- -------------------------------------------------------------------------

banner("Real-Time Playback")
print("  Switching to MODE_REALTIME")
dev:mode(drv2605.MODE_REALTIME)

local levels = { {32, "25%"}, {64, "50%"}, {96, "75%"}, {127, "100%"} }
for _, l in ipairs(levels) do
    local val, label = l[1], l[2]
    print(string.format("  RTP amplitude %s for 400 ms", label))
    dev:rtpvalue(val)
    sleep(400)
end

print("  Ramping down to zero")
dev:rtpvalue(0)
sleep(100)

-- Read back to confirm the register was written
check("rtpvalue reads back 0", dev:rtpvalue(), 0)

-- Return to internal trigger mode for safety
dev:mode(drv2605.MODE_INTTRIG)
check("mode restored after RTP", dev:mode(), drv2605.MODE_INTTRIG)

-- -------------------------------------------------------------------------
-- Stop function test: start a long effect then stop early
-- -------------------------------------------------------------------------

banner("Explicit stop() During Playback")
print("  Starting effect 14 (strong buzz), stopping after 300 ms")
dev:setwaveform(0, 14)
dev:setwaveform(1,  0)
dev:play()
sleep(300)
dev:stop()
print("  Stopped")
sleep(500)

-- -------------------------------------------------------------------------
-- Done
-- -------------------------------------------------------------------------

banner("Summary")
print("All tests complete.")
print(string.format("Final state — mode: %d  library: %d",
      dev:mode(), dev:library()))
