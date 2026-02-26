-- task_table_pass.lua
-- Demonstrates creating a Lua table in one FreeRTOS task and passing it
-- to another task using a shared global + mutex for safety and events
-- for synchronisation.
--
-- Architecture notes:
--   * In Lua-RTOS each thread has its own lua_State but all share the same
--     global environment, so a table written to a global in one thread is
--     visible in all others.
--   * event:broadcast() only delivers to threads that are already registered
--     as listeners (i.e. have called wait() at least once).  A broadcast
--     fired before the peer has called wait() is silently dropped.
--   * Fix: producer calls data_consumed:wait() FIRST (before it ever
--     produces), then the main script kicks the cycle off only AFTER a
--     short sleep that guarantees both threads have registered.

-- ── Shared state ──────────────────────────────────────────────────────────
shared_table  = nil
table_mutex   = thread.createmutex()
data_ready    = event.create()   -- producer → consumer: "table is ready"
data_consumed = event.create()   -- consumer → producer / main: "ready for next"

-- ── Producer task ─────────────────────────────────────────────────────────
-- Waits for the "go" signal (first from main, then from consumer), builds a
-- table, deposits it in shared_table, and signals the consumer.
producer = thread.start(function()
    local seq = 0

    while true do
        -- Block here first: listener for data_consumed is registered on this
        -- very first wait() call, which is what lets the main-thread kick work.
        data_consumed:wait()

        seq = seq + 1

        local t = {
            seq      = seq,
            message  = "hello from producer, seq=" .. seq,
            readings = { voltage = 3.3 + seq * 0.01,
                         current = 0.12 + seq * 0.001 },
        }

        table_mutex:lock()
        shared_table = t
        table_mutex:unlock()

        -- Consumer is already in data_ready:wait() so this lands immediately.
        data_ready:broadcast()

        tmr.delayms(500)    -- pace before waiting for next "go"
    end
end, 10240, 5, 0, "producer")

-- ── Consumer task ─────────────────────────────────────────────────────────
-- Registers for data_ready on the first wait(), then loops: read table,
-- print, signal producer.
consumer = thread.start(function()
    while true do
        data_ready:wait()

        table_mutex:lock()
        local t = shared_table
        table_mutex:unlock()

        if t then
            print(string.format(
                "[consumer] seq=%d  msg='%s'  V=%.3f  I=%.4f",
                t.seq,
                t.message,
                t.readings.voltage,
                t.readings.current))
        end

        data_consumed:broadcast()
    end
end, 10240, 5, 1, "consumer")   -- pin consumer to core 1

print(string.format("producer thread id=%d", producer))
print(string.format("consumer thread id=%d", consumer))

-- Both threads must call wait() at least once before we broadcast, otherwise
-- the signal lands on an empty listener list and is dropped.  A short sleep
-- yields the CPU so FreeRTOS can schedule both threads until they block.
tmr.delayms(200)

-- Kick off the first cycle.  Producer is now in data_consumed:wait() and
-- consumer is in data_ready:wait(), so the handshake is race-condition-free
-- from here on.
data_consumed:broadcast()

-- Keep the main thread alive so that Ctrl-C (SIGINT) reaches this Lua state.
-- SIGINT sets a debug hook that raises "interrupted!" on the next VM
-- instruction; pcall catches that error and we can then stop the threads.
-- Without this loop, dofile() would return and SIGINT would only interrupt
-- the idle REPL prompt, leaving the background threads running forever.
print("Tasks running -- press Ctrl-C to stop")
pcall(function()
    while true do
        tmr.delayms(100)
    end
end)

thread.stop(producer)
thread.stop(consumer)
print("Stopped.")
