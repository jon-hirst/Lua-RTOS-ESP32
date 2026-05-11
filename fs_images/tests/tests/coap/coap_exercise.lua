-- coap_exercise.lua
-- Self-contained CoAP server exercise: starts a server, then drives it with
-- client calls from the same script to verify every method and error path.
--
-- The server runs in a background FreeRTOS task (started by coap.server()).
-- All client calls use loopback (127.0.0.1:5683).

local HOST = "coap://127.0.0.1"
local PORT = 5683

-- ------------------------------------------------------------------ helpers

local passed = 0
local failed = 0

local function check(name, cond, got, expected)
    if cond then
        passed = passed + 1
        print(string.format("  PASS  %s", name))
    else
        failed = failed + 1
        print(string.format("  FAIL  %s  (got=%s  expected=%s)",
              name, tostring(got), tostring(expected)))
    end
end

local function eq(a, b)   return a == b end
local function ne(a, b)   return a ~= b end
local function nonempty(s) return type(s) == "string" and #s > 0 end

-- ------------------------------------------------------------------ server

print("=== Starting CoAP server on port " .. PORT .. " ===")

local srv = coap.server(PORT)

-- State shared between handlers and client tests
local counter      = 0
local stored_data  = ""
local delete_count = 0

-- /info  –  GET only: returns a small JSON object
local res_info = srv:resource("info")

res_info:on("get", function(payload, cf)
    local body = string.format(
        '{"name":"Lua-RTOS","port":%d,"counter":%d}', PORT, counter)
    return body, coap.APP_JSON
end)

-- /counter  –  GET returns count, POST increments, DELETE resets
local res_counter = srv:resource("counter")

res_counter:on("get", function(payload, cf)
    return tostring(counter), coap.TEXT_PLAIN
end)

res_counter:on("post", function(payload, cf)
    local step = tonumber(payload) or 1
    counter = counter + step
    return tostring(counter), coap.TEXT_PLAIN
end)

res_counter:on("delete", function(payload, cf)
    delete_count = delete_count + 1
    counter = 0
    -- no response body for DELETE → 2.02 Deleted (set by the default dispatch)
end)

-- /data  –  GET/PUT for a simple key-value store
local res_data = srv:resource("data")

res_data:on("get", function(payload, cf)
    if #stored_data == 0 then
        return "(empty)", coap.TEXT_PLAIN
    end
    return stored_data, coap.TEXT_PLAIN
end)

res_data:on("put", function(payload, cf)
    stored_data = payload or ""
    -- 2.04 Changed: return nil so no body is sent
end)

res_data:on("post", function(payload, cf)
    -- Append to stored_data
    stored_data = stored_data .. (payload or "")
    return stored_data, coap.TEXT_PLAIN
end)

-- Small delay so the server task is scheduled before we start sending
thread.sleep(100)

-- ================================================================== tests

print("\n=== GET /info ===")
do
    local body, code, cf = coap.get(HOST .. "/info")
    check("response code is 2.05 Content", eq(code, coap.CONTENT), code, coap.CONTENT)
    check("content-format is APP_JSON",     eq(cf,   coap.APP_JSON), cf, coap.APP_JSON)
    check("body contains 'Lua-RTOS'",       nonempty(body) and body:find("Lua-RTOS") ~= nil,
          body, "<json with Lua-RTOS>")
    print("  body: " .. tostring(body))
end

print("\n=== POST /counter (increment by 1) ===")
do
    local body, code = coap.post(HOST .. "/counter", "1", coap.TEXT_PLAIN)
    check("response code is 2.01 Created", eq(code, coap.CREATED), code, coap.CREATED)
    check("counter is now 1",              eq(body, "1"),           body, "1")
end

print("\n=== POST /counter (increment by 4) ===")
do
    local body, code = coap.post(HOST .. "/counter", "4", coap.TEXT_PLAIN)
    check("response code is 2.01 Created", eq(code, coap.CREATED), code, coap.CREATED)
    check("counter is now 5",              eq(body, "5"),           body, "5")
end

print("\n=== GET /counter ===")
do
    local body, code = coap.get(HOST .. "/counter")
    check("response code is 2.05 Content", eq(code, coap.CONTENT), code, coap.CONTENT)
    check("counter value is 5",            eq(body, "5"),           body, "5")
end

print("\n=== DELETE /counter (reset) ===")
do
    local body, code = coap.delete(HOST .. "/counter")
    check("response code is 2.02 Deleted", eq(code, coap.DELETED), code, coap.DELETED)
    check("delete_count incremented",       eq(delete_count, 1),    delete_count, 1)
end

print("\n=== GET /counter after DELETE ===")
do
    local body, code = coap.get(HOST .. "/counter")
    check("counter reset to 0", eq(body, "0"), body, "0")
end

print("\n=== GET /data (initially empty) ===")
do
    local body, code = coap.get(HOST .. "/data")
    check("response is 2.05 Content", eq(code, coap.CONTENT), code, coap.CONTENT)
    check("empty store marker",       eq(body, "(empty)"),     body, "(empty)")
end

print("\n=== PUT /data ===")
do
    local body, code = coap.put(HOST .. "/data", "hello world", coap.TEXT_PLAIN)
    check("PUT returns 2.04 Changed", eq(code, coap.CHANGED), code, coap.CHANGED)
end

print("\n=== GET /data after PUT ===")
do
    local body, code = coap.get(HOST .. "/data")
    check("body matches PUT value", eq(body, "hello world"), body, "hello world")
end

print("\n=== POST /data (append) ===")
do
    local body, code = coap.post(HOST .. "/data", "!!!", coap.TEXT_PLAIN)
    check("POST returns 2.01 Created",      eq(code, coap.CREATED),       code, coap.CREATED)
    check("body is appended",               eq(body, "hello world!!!"),    body, "hello world!!!")
end

print("\n=== GET unknown resource → 4.04 Not Found ===")
do
    local body, code = coap.get(HOST .. "/no/such/resource")
    check("returns 4.04 Not Found", eq(code, coap.NOT_FOUND), code, coap.NOT_FOUND)
end

print("\n=== Wrong method on /info → 4.05 Not Allowed ===")
do
    -- /info has only a GET handler; PUT should get 4.05
    local body, code = coap.put(HOST .. "/info", "x", coap.TEXT_PLAIN)
    check("returns 4.05 Not Allowed", eq(code, coap.NOT_ALLOWED), code, coap.NOT_ALLOWED)
end

print("\n=== Observe /counter ===")
do
    -- Reset counter first so notifications are predictable
    coap.delete(HOST .. "/counter")

    local notifs = {}
    local obs = coap.observe(HOST .. "/counter", function(payload, code)
        table.insert(notifs, payload)
    end)

    -- POST three increments; each should send an Observe notification
    thread.sleep(50)
    coap.post(HOST .. "/counter", "10", coap.TEXT_PLAIN)
    thread.sleep(300)
    coap.post(HOST .. "/counter", "10", coap.TEXT_PLAIN)
    thread.sleep(300)
    coap.post(HOST .. "/counter", "10", coap.TEXT_PLAIN)
    thread.sleep(500)

    obs:cancel()

    -- We expect at least the initial GET response notification
    check("at least one notification received", #notifs >= 1, #notifs, ">= 1")
    print("  notifications received: " .. #notifs)
    for i, v in ipairs(notifs) do
        print(string.format("    [%d] %s", i, tostring(v)))
    end
end

-- ================================================================== teardown

print("\n=== Stopping server ===")
srv:stop()

-- ================================================================== summary

print(string.format("\n=== Results: %d passed, %d failed ===", passed, failed))
if failed > 0 then
    error(string.format("%d test(s) failed", failed))
end
