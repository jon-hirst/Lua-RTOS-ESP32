-- test_sd.lua: SD card driver smoke test
-- Exercises sd.mount, sd.info, file I/O through the FAT VFS, and sd.umount.
-- Run on hardware with an SD card wired to the configured SDMMC or SPI pins.

local MOUNT_PATH = "/sd"
local TEST_FILE  = MOUNT_PATH .. "/test_sd.txt"
local TEST_DATA  = "Hello from Lua RTOS SD test!\n"

-- Step 1: mount the SD card
print("Mounting SD card at " .. MOUNT_PATH .. " ...")
sd.mount(MOUNT_PATH)
print("Mount OK")

-- Step 2: print card information
print("Card info:")
local info = sd.info()
print("  name    : " .. tostring(info.name))
print("  type    : " .. tostring(info.type))
print("  size_mb : " .. tostring(info.size_mb) .. " MB")

-- Step 3: write a test file
print("Writing " .. TEST_FILE .. " ...")
local fh, err = io.open(TEST_FILE, "w")
assert(fh, "open for write failed: " .. tostring(err))
fh:write(TEST_DATA)
fh:close()
print("Write OK")

-- Step 4: read it back and verify
print("Reading " .. TEST_FILE .. " ...")
fh, err = io.open(TEST_FILE, "r")
assert(fh, "open for read failed: " .. tostring(err))
local readback = fh:read("*a")
fh:close()
assert(readback == TEST_DATA,
    "data mismatch: expected " .. TEST_DATA .. " got " .. tostring(readback))
print("Read OK — data matches")

-- Step 5: delete the test file
print("Deleting " .. TEST_FILE .. " ...")
local ok, msg = os.remove(TEST_FILE)
assert(ok, "remove failed: " .. tostring(msg))
print("Delete OK")

-- Step 6: unmount
print("Unmounting " .. MOUNT_PATH .. " ...")
sd.umount(MOUNT_PATH)
print("Unmount OK")

print("SD card test PASSED")
