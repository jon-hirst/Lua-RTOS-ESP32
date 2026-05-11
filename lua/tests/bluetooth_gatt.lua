-- bluetooth_gatt.lua  --  GATT server and client smoke test
--
-- Server path: registers a Heart Rate service (UUID 0x180D), adds a
-- Measurement characteristic (UUID 0x2A37), starts advertising for 10 s,
-- then stops.
--
-- Client path: change CLIENT_ADDR to the hex address of the peripheral you
-- want to connect to.  Set RUN_CLIENT = true to enable the client path.
--
-- Usage:   dofile("lua/tests/bluetooth_gatt.lua")

local RUN_CLIENT = false
local CLIENT_ADDR = "AABBCCDDEEFF"   -- replace with real address (no colons)

-- Characteristic properties and permissions (from esp_gatt_defs.h)
local PROP_READ   = 0x02
local PROP_WRITE  = 0x08
local PROP_NOTIFY = 0x10

local PERM_READ   = 0x01
local PERM_WRITE  = 0x02

-- ==========================================================================
-- SERVER PATH
-- ==========================================================================

print("=== GATT server test ===")

-- 1. Initialise BLE stack in BLE mode
bluetooth.attach(bluetooth.mode.BLE)

-- 2. Register application (app_id is arbitrary)
bluetooth.gatts.register(0x55)
print("GATTS registered")

-- 3. Create the Heart Rate service (0x180D, 4 attribute handles)
local srv = bluetooth.gatts.create_service(0x180D, 4)
print("Service handle:", srv)

-- 4. Add the Heart Rate Measurement characteristic (0x2A37)
--    Initial value: 0x0000 (flags + heart-rate bytes)
local char_hdl = bluetooth.gatts.add_char(srv, 0x2A37,
                     PROP_READ + PROP_WRITE + PROP_NOTIFY,
                     PERM_READ + PERM_WRITE,
                     "0000")
print("Characteristic handle:", char_hdl)

-- 5. Register read callback
bluetooth.gatts.on_read(char_hdl, function(ev)
    print(string.format("READ  conn=%d trans=%d handle=%d offset=%d",
          ev.conn_id, ev.trans_id, ev.handle, ev.offset))
    -- Reply with a static heart-rate value of 72 bpm: flags=0x00, hr=0x48
    bluetooth.gatts.send_response(ev.conn_id, ev.trans_id, ev.handle, "0048")
end)

-- 6. Register write callback
bluetooth.gatts.on_write(char_hdl, function(ev)
    print(string.format("WRITE conn=%d handle=%d value=%s need_rsp=%s",
          ev.conn_id, ev.handle, ev.value, tostring(ev.need_rsp)))
end)

-- 7. Register connect / disconnect callbacks
bluetooth.gatts.on_connect(function(ev)
    print("Central connected: conn_id=" .. ev.conn_id .. " addr=" .. ev.addr)
end)

bluetooth.gatts.on_disconnect(function(ev)
    print("Central disconnected: conn_id=" .. ev.conn_id)
end)

-- 8. Start the service
bluetooth.gatts.start_service(srv)
print("Service started")

-- 9. Advertise so a central can find us (undirected connectable)
--    Minimal advertising data: flags (02 01 06) + complete name (Lua-HR)
local adv_data = "02010607094c75612d4852"
bluetooth.advertise.start(
    100,                             -- interval_min ms
    100,                             -- interval_max ms
    bluetooth.adv.ADV_IND,
    bluetooth.ownaddr.Public,
    bluetooth.peeraddr.Public,
    "000000000000",                  -- peer address (unused for ADV_IND)
    bluetooth.chann.All,
    bluetooth.filter.ConnAllScanAll,
    adv_data
)
print("Advertising for 10 seconds …")
sys.delay(10000)

bluetooth.advertise.stop()
bluetooth.gatts.stop_service(srv)
print("Server stopped")

-- ==========================================================================
-- CLIENT PATH (disabled by default)
-- ==========================================================================

if not RUN_CLIENT then
    print("Client path skipped (set RUN_CLIENT = true to enable)")
    return
end

print("\n=== GATT client test ===")

-- 1. Register GATTC application
bluetooth.gattc.register(0x66)
print("GATTC registered")

-- 2. Set up persistent callbacks before connecting
bluetooth.gattc.on_connect(function(ev)
    print("Connected to " .. ev.addr .. " conn_id=" .. ev.conn_id)
end)

bluetooth.gattc.on_disconnect(function(ev)
    print("Disconnected from conn_id=" .. ev.conn_id)
end)

bluetooth.gattc.on_notify(function(ev)
    print(string.format("NOTIFY conn=%d handle=%d value=%s",
          ev.conn_id, ev.handle, ev.value))
end)

-- 3. Connect (blocks until MTU negotiated)
print("Connecting to " .. CLIENT_ADDR .. " …")
local conn_id = bluetooth.gattc.connect(CLIENT_ADDR, 0)
print("Connected, conn_id=" .. conn_id)

-- 4. Discover services (filter for Heart Rate 0x180D)
print("Searching for service 0x180D …")
local svcs = bluetooth.gattc.search_service(conn_id, 0x180D)
if #svcs == 0 then
    print("Heart Rate service not found")
    bluetooth.gattc.disconnect(conn_id)
    return
end
local svc = svcs[1]
print(string.format("Found service uuid=%s start=%d end=%d",
      svc.uuid, svc.start_handle, svc.end_handle))

-- 5. Find the Heart Rate Measurement characteristic (0x2A37)
local char_h = bluetooth.gattc.get_char(conn_id,
                   svc.start_handle, svc.end_handle, 0x2A37)
print("Char handle:", char_h)

-- 6. Read it once
bluetooth.gattc.read_char(conn_id, char_h, function(ev)
    print("Read result: handle=" .. ev.handle .. " value=" .. ev.value
          .. " status=" .. ev.status)
end)
-- Give the callback a moment to arrive
sys.delay(1000)

-- 7. Register for notifications
bluetooth.gattc.register_notify(conn_id, char_h)
print("Registered for notifications, waiting 5 s …")
sys.delay(5000)

-- 8. Disconnect
bluetooth.gattc.disconnect(conn_id)
print("Disconnected")
