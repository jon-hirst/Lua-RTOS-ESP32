-- BMA423 accelerometer driver test
--
-- Exercises every method exposed by the bma423 Lua module:
--   setup, loadconfig, enablefeatures, xyz, temperature, steps,
--   range, enableinterrupt, intstatus, disableinterrupt
--
-- Hardware assumptions:
--   BMA423 on I2C0 (SDA=GPIO10, SCL=GPIO11), INT1 → GPIO14
--   bma423conf.bin at /bma423conf.bin

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
        print(string.format("  OK  %s = %.4f (in [%.4f, %.4f])", label, val, lo, hi))
    else
        print(string.format("  FAIL %s = %.4f (expected [%.4f, %.4f])", label, val, lo, hi))
    end
end

-- -------------------------------------------------------------------------
-- Setup
-- -------------------------------------------------------------------------

banner("Setup")
print("Initialising BMA423 on I2C0 (SDA=GPIO10, SCL=GPIO11)...")
local dev = bma423.setup()
print("OK")

-- -------------------------------------------------------------------------
-- Accelerometer constants
-- -------------------------------------------------------------------------

banner("Module Constants")
print(string.format("  RANGE_2G   = %d", bma423.RANGE_2G))
print(string.format("  RANGE_4G   = %d", bma423.RANGE_4G))
print(string.format("  RANGE_8G   = %d", bma423.RANGE_8G))
print(string.format("  RANGE_16G  = %d", bma423.RANGE_16G))
print(string.format("  INT_STEP     = 0x%02X", bma423.INT_STEP))
print(string.format("  INT_ACTIVITY = 0x%02X", bma423.INT_ACTIVITY))
print(string.format("  INT_TILT     = 0x%02X", bma423.INT_TILT))
print(string.format("  INT_ANY_NONE = 0x%02X", bma423.INT_ANY_NONE))

-- -------------------------------------------------------------------------
-- Range: default read-back and all four settings
-- -------------------------------------------------------------------------

banner("Range: default")
check("default range", dev:range(), bma423.RANGE_2G)

banner("Range: cycle through all four settings")
for _, r in ipairs({ bma423.RANGE_4G, bma423.RANGE_8G,
                     bma423.RANGE_16G, bma423.RANGE_2G }) do
    dev:range(r)
    check(string.format("range set to %dg", r), dev:range(), r)
end
-- Leave at 2 g for the rest of the test

-- -------------------------------------------------------------------------
-- XYZ acceleration (device at rest on a flat surface)
-- -------------------------------------------------------------------------

banner("XYZ Acceleration (device at rest, 2 g range)")
local x, y, z = dev:xyz()
print(string.format("  x = %8.4f g", x))
print(string.format("  y = %8.4f g", y))
print(string.format("  z = %8.4f g", z))

-- At rest the magnitude of the vector should be close to 1 g.
local mag = math.sqrt(x*x + y*y + z*z)
print(string.format("  |a| = %.4f g  (expect ~1.0)", mag))
checkrange("|a| ≈ 1 g", mag, 0.7, 1.3)

-- -------------------------------------------------------------------------
-- Temperature
-- -------------------------------------------------------------------------

banner("Die Temperature")
local temp = dev:temperature()
if temp == nil then
    print("  temperature: nil (sensor returned invalid reading)")
else
    print(string.format("  temperature: %d °C", temp))
    checkrange("temperature in plausible range", temp, -10, 85)
end

-- -------------------------------------------------------------------------
-- Interrupt status (without interrupt attached yet)
-- -------------------------------------------------------------------------

banner("Interrupt Status (polled, no ISR attached)")
local s0, s1 = dev:intstatus()
print(string.format("  INT_STATUS_0 = 0x%02X", s0))
print(string.format("  INT_STATUS_1 = 0x%02X", s1))

-- -------------------------------------------------------------------------
-- Load features configuration blob
-- -------------------------------------------------------------------------

banner("Load Features Configuration")
print("  Uploading /bma423conf.bin ...")
dev:loadconfig("/bma423conf.bin")
print("  OK — features engine initialised")

-- -------------------------------------------------------------------------
-- Enable step counter
-- -------------------------------------------------------------------------

banner("Enable Step Counter")
dev:enablefeatures("step-count")
print("  Step counter enabled")

-- -------------------------------------------------------------------------
-- Step count readback
-- -------------------------------------------------------------------------

banner("Step Count")
local steps = dev:steps()
print(string.format("  steps = %d", steps))
-- We can't assert an exact value, but it must be a non-negative integer.
if steps >= 0 then
    print("  OK  steps is non-negative")
else
    print("  FAIL steps is negative")
end

-- -------------------------------------------------------------------------
-- XYZ after config load (features engine running)
-- -------------------------------------------------------------------------

banner("XYZ After Config Load")
x, y, z = dev:xyz()
print(string.format("  x = %8.4f g", x))
print(string.format("  y = %8.4f g", y))
print(string.format("  z = %8.4f g", z))
mag = math.sqrt(x*x + y*y + z*z)
checkrange("|a| ≈ 1 g", mag, 0.7, 1.3)

-- -------------------------------------------------------------------------
-- Interrupt: attach, wait for data-ready events, then detach
-- -------------------------------------------------------------------------

banner("Interrupt: enable on GPIO14 (INT1), data-ready")

local irq_count = 0
local irq_s0_last = 0
local irq_s1_last = 0

dev:enableinterrupt(function(s0_cb, s1_cb)
    irq_count = irq_count + 1
    irq_s0_last = s0_cb
    irq_s1_last = s1_cb
end)

print("  Waiting 500 ms for data-ready interrupts...")
sleep(500)

dev:disableinterrupt()

print(string.format("  Received %d interrupt(s)", irq_count))
print(string.format("  Last INT_STATUS_0 = 0x%02X", irq_s0_last))
print(string.format("  Last INT_STATUS_1 = 0x%02X", irq_s1_last))

-- At 100 Hz we expect ~50 interrupts in 500 ms; accept anything > 0.
if irq_count > 0 then
    print("  OK  at least one interrupt received")
else
    print("  WARN no interrupts received (check GPIO14 wiring)")
end

-- -------------------------------------------------------------------------
-- Polled XYZ burst: 5 samples at 100 ms intervals
-- -------------------------------------------------------------------------

banner("Polled Burst: 5 samples at 100 ms")
for i = 1, 5 do
    x, y, z = dev:xyz()
    temp = dev:temperature()
    print(string.format("  [%d] x=%7.4f  y=%7.4f  z=%7.4f  temp=%s",
          i, x, y, z, temp ~= nil and tostring(temp).."°C" or "nil"))
    sleep(100)
end

-- -------------------------------------------------------------------------
-- Step count after walking (informational)
-- -------------------------------------------------------------------------

banner("Step Count (final)")
print(string.format("  steps = %d", dev:steps()))

-- -------------------------------------------------------------------------
-- Done
-- -------------------------------------------------------------------------

banner("Summary")
print("All tests complete.")
print(string.format("Final range: %d g", dev:range()))
