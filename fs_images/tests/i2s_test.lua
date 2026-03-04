-- I2S test: play middle C (261 Hz) for 1 second via a PCM DAC/amplifier.
--
-- Wiring (adjust pins to match your hardware):
--   PIN_SCK  -> I2S BCLK (bit clock)
--   PIN_WS   -> I2S LRCLK / WS (word select)
--   PIN_SD   -> I2S DOUT (serial data out)
--
-- The generated signal is a 16-bit signed, mono, little-endian sine wave
-- at 44100 Hz.  Feed it to any I2S DAC (e.g. MAX98357A, PCM5102, etc.).

local PIN_SCK  = 48     -- bit clock GPIO
local PIN_WS   = 15     -- word-select GPIO
local PIN_SD   = 46     -- data-out GPIO

local RATE     = 44100  -- sample rate (Hz)
local BITS     = 16     -- sample width (bits)
local BUFLEN   = 1024   -- DMA buffer size (bytes)

local FREQ     = 261    -- middle C (C4 ≈ 261.63 Hz)
local DURATION = 1      -- seconds to play

-- ---------------------------------------------------------------------------
-- Pre-generate exactly one period of the sine wave as a fixed byte string.
--
-- Using floor(RATE/FREQ + 0.5) gives the nearest integer number of samples
-- to one full cycle.  For 261 Hz @ 44100 Hz that is 169 samples = 338 bytes.
-- Writing this small string repeatedly avoids allocating thousands of tiny
-- strings per second, which would fragment the TLSF heap and crash.
-- ---------------------------------------------------------------------------
local period_samples = math.floor(RATE / FREQ + 0.5)
local amplitude      = 32767   -- peak for signed 16-bit
local two_pi_f       = 2 * math.pi * FREQ

print(string.format("Pre-generating one period (%d samples)...", period_samples))

local bytes = {}
for i = 0, period_samples - 1 do
    local s = math.floor(amplitude * math.sin(two_pi_f * i / RATE))
    if s < 0 then s = s + 65536 end          -- map to unsigned 16-bit
    bytes[#bytes + 1] = string.char(s & 0xFF, (s >> 8) & 0xFF)
end
local period_str = table.concat(bytes)
bytes = nil   -- allow GC to reclaim the temporary table

-- ---------------------------------------------------------------------------
-- Open the I2S channel and stream the period string for DURATION seconds.
-- ---------------------------------------------------------------------------
print("Opening I2S channel...")
local dev = i2s.attach(PIN_SCK, PIN_WS, PIN_SD, i2s.TX, BITS, i2s.MONO, RATE, BUFLEN)

-- Number of whole periods that fit inside DURATION seconds
local total_samples  = RATE * DURATION
local repeats        = math.ceil(total_samples / period_samples)

print(string.format("Playing %d Hz for %d second(s) (%d repeats of %d-sample period)...",
      FREQ, DURATION, repeats, period_samples))

for _ = 1, repeats do
    dev:write(period_str)
end

dev:detach()
print("Done.")
