-- CoAP server test: registers /test/hello with GET and PUT handlers.
-- Run this before coap_client_test.lua (or in a separate thread).
-- The server runs for 30 seconds then stops.

local stored_value = "Hello CoAP"

local srv = coap.server(5683)

local res = srv:resource("test/hello")

res:on("get", function(payload, cf)
    return stored_value, coap.TEXT_PLAIN
end)

res:on("put", function(payload, cf)
    if payload and #payload > 0 then
        stored_value = payload
    end
    return nil  -- 2.04 Changed, no body
end)

print("CoAP server running on port 5683 for 30 seconds...")
thread.sleep(30000)

srv:stop()
print("CoAP server stopped.")
