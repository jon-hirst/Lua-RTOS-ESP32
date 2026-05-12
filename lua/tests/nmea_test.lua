-- nmea_test.lua  --  NMEA sentence parser smoke test
--
-- Opens the nmea module on UART 1 at 9600 baud (standard GPS baud rate),
-- waits 2 seconds for the GPS to produce a fix, reads position data, then
-- closes the port.
--
-- Usage:   dofile("lua/tests/nmea_test.lua")

local UART_ID = 1
local BAUD    = 9600

print("=== NMEA parser test ===")

-- 1. Open the NMEA module (opens UART, spawns background reader task)
local ok, inst = pcall(nmea.setup, UART_ID, BAUD)
if not ok then
    print("nmea.setup failed: " .. tostring(inst))
    return
end
print(string.format("Opened UART %d at %d baud", UART_ID, BAUD))

-- 2. Wait 2 seconds for the GPS to produce at least one sentence
print("Waiting 2 s for GPS sentences …")
tmr.sleep(2)

-- 3. Read position data
local ok2, pos = pcall(function() return inst:read() end)
if not ok2 then
    print("inst:read() failed: " .. tostring(pos))
else
    print(string.format("valid  = %s", tostring(pos.valid)))
    print(string.format("lat    = %.6f", pos.lat))
    print(string.format("lon    = %.6f", pos.lon))
    print(string.format("height = %.1f m", pos.height))
    print(string.format("sats   = %d", pos.sats))
end

-- 4. Close the port and stop the background task
local ok3, err = pcall(function() inst:close() end)
if not ok3 then
    print("inst:close() failed: " .. tostring(err))
else
    print("Port closed")
end

print("=== NMEA test done ===")
