-- WPA3 WiFi authentication smoke test.
-- Tests that WPA3 constants exist and that net.wf.setup() accepts the auth argument
-- without error.  Actual AP connection requires hardware and a WPA3-capable AP.

local ssid = "test_ssid"
local pass = "test_password"

-- 1. Verify constants are integers and non-zero
assert(type(net.wf.auth.WPA3_PSK)      == "number", "WPA3_PSK should be a number")
assert(type(net.wf.auth.WPA2_WPA3_PSK) == "number", "WPA2_WPA3_PSK should be a number")
assert(net.wf.auth.WPA3_PSK      ~= 0, "WPA3_PSK should be non-zero")
assert(net.wf.auth.WPA2_WPA3_PSK ~= 0, "WPA2_WPA3_PSK should be non-zero")
assert(net.wf.auth.WPA3_PSK ~= net.wf.auth.WPA2_WPA3_PSK, "WPA3_PSK and WPA2_WPA3_PSK should be distinct")

-- 2. net.wf.setup() with WPA3_PSK should not raise a Lua error
local ok, err = pcall(net.wf.setup, net.wf.mode.STA, ssid, pass,
                      0, 0, 0, 0, 0, 0, 0, net.wf.auth.WPA3_PSK)
assert(ok, "net.wf.setup with WPA3_PSK raised an error: " .. tostring(err))

-- 3. net.wf.setup() with WPA2_WPA3_PSK should not raise a Lua error
local ok2, err2 = pcall(net.wf.setup, net.wf.mode.STA, ssid, pass,
                        0, 0, 0, 0, 0, 0, 0, net.wf.auth.WPA2_WPA3_PSK)
assert(ok2, "net.wf.setup with WPA2_WPA3_PSK raised an error: " .. tostring(err2))

print("test_wifi_wpa3: all assertions passed")
