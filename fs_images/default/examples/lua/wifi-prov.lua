-- wifi-prov.lua
--
-- Demonstrates the WiFi captive-portal provisioning feature (net.wf.prov).
--
-- User flow:
--   1. Run this script on a freshly-flashed device (or after erasing credentials).
--   2. The device starts a SoftAP named "LuaRTOS-Setup" (open, no password).
--   3. Connect your phone or PC to that network.
--   4. Open any URL in a browser — the captive portal redirects to the setup form.
--   5. Select your home network SSID from the list (or type it manually) and enter
--      the password, then tap "Save & Connect".
--   6. The device saves the credentials to NVS and reboots.  On the next boot,
--      system.lua will read those credentials and connect automatically.
--
-- Requires CONFIG_LUA_RTOS_WIFI_PROV=y in sdkconfig (menuconfig → Lua RTOS).

if net.wf.prov == nil then
    print("WiFi provisioning support is not compiled in.")
    print("Enable CONFIG_LUA_RTOS_WIFI_PROV in menuconfig and rebuild.")
    return
end

-- Check whether credentials are already stored in NVS.
if net.wf.prov.has_credentials() then
    print("WiFi credentials are already stored in NVS.")
    print("To force re-provisioning, call net.wf.prov.reset() and reboot.")
else
    print("No credentials found.  Starting provisioning portal ...")
    print("Connect to WiFi network 'LuaRTOS-Setup', then open any URL in your browser.")
    -- This call starts the SoftAP + captive DNS + HTTP form server.
    -- It blocks until the user submits credentials and the device reboots.
    net.wf.prov.start("LuaRTOS-Setup", "")
end
