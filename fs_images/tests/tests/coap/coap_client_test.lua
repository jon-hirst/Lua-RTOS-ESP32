-- CoAP client test: exercises GET, PUT, and Observe against the server test.
-- Requires coap_server_test.lua to be running (start it in another thread first).

local function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("%s: expected %s, got %s", msg or "assert", tostring(b), tostring(a)))
    end
end

-- Allow server a moment to start if launched just before this test
thread.sleep(200)

-- GET the default value
local payload, code, cf = coap.get("coap://127.0.0.1/test/hello")
assert(payload ~= nil, "GET should not return nil")
assert_eq(code,    coap.CONTENT, "GET response code should be 2.05 Content")
assert_eq(payload, "Hello CoAP",  "GET payload should be 'Hello CoAP'")
print("GET test passed: payload=" .. payload)

-- PUT a new value
local _, put_code = coap.put("coap://127.0.0.1/test/hello", "Updated Value", coap.TEXT_PLAIN)
assert_eq(put_code, coap.CHANGED, "PUT response code should be 2.04 Changed")
print("PUT test passed: code=" .. tostring(put_code))

-- GET again to verify the update
payload, code, cf = coap.get("coap://127.0.0.1/test/hello")
assert_eq(code,    coap.CONTENT,     "second GET response code should be 2.05")
assert_eq(payload, "Updated Value",  "second GET payload should be 'Updated Value'")
print("GET after PUT test passed: payload=" .. payload)

-- Observe: subscribe, receive two notifications, then cancel
local notifications = {}
local obs = coap.observe("coap://127.0.0.1/test/hello", function(p, c)
    table.insert(notifications, p)
end)

-- PUT twice to trigger notifications
coap.put("coap://127.0.0.1/test/hello", "Notif1", coap.TEXT_PLAIN)
thread.sleep(500)
coap.put("coap://127.0.0.1/test/hello", "Notif2", coap.TEXT_PLAIN)
thread.sleep(1000)

obs:cancel()
print("Observe received " .. #notifications .. " notification(s)")

print("All CoAP client tests passed.")
