-- WebSocket server and client smoke-test.
--
-- 1. Connects WiFi (edit SSID / password below).
-- 2. Starts a WebSocket server on port 8080 that echoes every message back.
-- 3. Creates a WebSocket client that connects to ws://127.0.0.1:8080/, sends
--    "hello", and prints the echoed reply.
-- 4. Prints the device IP so you can also connect from a browser or wscat:
--      wscat -c ws://<device-ip>:8080/

local SSID = "mywifi"
local PASS = "mypassword"
local PORT = 8080

-- Connect WiFi
net.wf.setup(net.wf.mode.STA, SSID, PASS)
net.wf.start()
print("IP: " .. net.wf.ip())

-- Start the echo server
local server = ws.server(PORT)
print("WebSocket server listening on port " .. PORT)

local client_fd = nil   -- file descriptor of the first connected client

server:on("open", function(fd)
    print("server: client connected, fd=" .. fd)
    client_fd = fd
end)

server:on("message", function(msg)
    print("server: received '" .. msg .. "', echoing back")
    if client_fd then
        server:send(client_fd, msg)
    end
end)

server:on("close", function(fd)
    print("server: client disconnected, fd=" .. fd)
    client_fd = nil
end)

-- Give the server a moment to start
tmr.delay(200000)   -- 200 ms

-- Create a client that connects to the local server
local client = ws.client("ws://127.0.0.1:" .. PORT .. "/")

client:on("open", function()
    print("client: connected")
    client:send("hello")
end)

client:on("message", function(msg)
    print("client: received echo '" .. msg .. "'")
    client:close()
end)

client:on("close", function()
    print("client: connection closed")
end)
