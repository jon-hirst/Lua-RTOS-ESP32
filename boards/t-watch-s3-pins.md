# T-Watch S3 — ESP32-S3 GPIO Pin Assignments

Source: t-watch-s3-pins.webp

---

## ST7789V LCD (1.54", 240×240)

| Signal | GPIO |
|--------|------|
| MISO   | —    |
| MOSI   | IO13 |
| CLK    | IO18 |
| CS     | IO12 |
| DC     | IO38 |
| RST    | —    |
| BL (Backlight) | IO45 |

---

## DRV2605 Haptic Motor

| Signal  | GPIO |
|---------|------|
| I2C_SDA | IO10 |
| I2C_SCL | IO11 |

---

## BMA423 Axis Sensor

| Signal    | GPIO |
|-----------|------|
| Interrupt | IO14 |
| I2C_SDA   | IO10 |
| I2C_SCL   | IO11 |

---

## RTC Clock (PCF8563)

| Signal    | GPIO |
|-----------|------|
| Interrupt | IO17 |
| I2C_SDA   | IO10 |
| I2C_SCL   | IO11 |

---

## PMU: AXP2101

| Signal    | GPIO |
|-----------|------|
| Interrupt | IO21 |
| I2C_SDA   | IO10 |
| I2C_SCL   | IO11 |

---

## LoRa: SX1262

| Signal     | GPIO |
|------------|------|
| RADIO_SCK  | IO3  |
| RADIO_MISO | IO4  |
| RADIO_MOSI | IO1  |
| RADIO_SS   | IO5  |
| RADIO_DIO1 | IO9  |
| RADIO_RST  | IO8  |
| RADIO_BUSY | IO7  |

---

## I2S Amplifier: MAX98357A

| Signal  | GPIO |
|---------|------|
| BCK     | IO48 |
| WS      | IO15 |
| DOUT    | IO46 |

---

## PDM Microphone

| Signal | GPIO |
|--------|------|
| Data   | IO47 |
| Sclk   | IO44 |

---

## Capacitive Touch

| Signal    | GPIO |
|-----------|------|
| Interrupt | IO16 |
| I2C_SDA   | IO39 |
| I2C_SCL   | IO40 |

---

## IR Transmitter/Receiver

| Signal | GPIO |
|--------|------|
| IR     | IO2  |

---

## Notes

- The DRV2605, BMA423, RTC (PCF8563), and PMU (AXP2101) all share the same I2C bus: SDA=IO10, SCL=IO11.
- The Capacitive Touch controller uses a separate I2C bus: SDA=IO39, SCL=IO40.
- LCD MISO and RST are not connected (write-only display, hardware reset not used).
- The Power Button is managed by the AXP2101 PMU (hold 2 s to power on, hold 6 s to power off).
