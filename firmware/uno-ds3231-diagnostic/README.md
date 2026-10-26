# Arduino Uno DS3231 standalone diagnostic

> **Deferred-hardware utility:** no RTC is populated or used by the production
> station. Keep this sketch only for separate DS3231 bench testing if that RTC
> is selected later.

This sketch tests the station's DS3231 separately from the NodeMCU and other
sensors. Its timekeeping rules match the production NodeMCU firmware where
they apply:

- hardware I2C at 100 kHz;
- DS3231 address `0x68`;
- the RTC stores UTC, not local time;
- dates before `2021-01-01T00:00:00Z` are invalid;
- India Standard Time is derived for display with a `+05:30` offset;
- `lostPower()` prevents an old RTC value from being considered trustworthy;
- setting a valid Unix epoch uses the same `rtc.adjust(DateTime(epoch))` path
  used when the NodeMCU disciplines the RTC from NTP or GPS.

The diagnostic does not use Wi-Fi, NTP, or GPS. It reports the raw RTC epoch,
UTC, IST, lost-power flag, chip temperature, and verifies over two seconds that
the clock is advancing. It then prints a live reading once per second.

## Wiring

Disconnect the DS3231 from the NodeMCU while running this test. The Uno must be
the only I2C controller connected to the module.

| DS3231 module | Arduino Uno |
| --- | --- |
| SDA | A4 / SDA |
| SCL | A5 / SCL |
| GND | GND |
| VCC | Module-rated supply |

Use pull-ups compatible with the module and every device on the temporary bus.
Do not add 5 V pull-ups if the module is later returned to the NodeMCU's 3.3 V
I2C bus. Many DS3231 boards also expose an EEPROM at `0x57`; seeing it is normal
and the sketch does not modify it.

## Build, upload, and monitor

```powershell
cd C:\Users\USER\Documents\ChatGPT\imd\firmware\uno-ds3231-diagnostic
pio run
pio run --target upload --upload-port COM12
pio device monitor --port COM12 --baud 115200
```

Replace `COM12` with the Uno port from `pio device list`. Set the serial monitor
line ending to **Newline** or **Both NL & CR**.

## Commands

| Command | Action |
| --- | --- |
| `r` | rerun scan, validity, temperature, and clock-advance tests |
| `p` | print one RTC reading |
| `l` | toggle one-reading-per-second output |
| `i` | scan the I2C bus |
| `u <epoch>` | set the RTC from a UTC Unix epoch and verify the write |
| `h` or `?` | print command help |

Example: `u 1791631800`. Only run the `u` command with the correct current UTC
epoch: it writes the RTC and clears the DS3231 oscillator-stop flag.

## Battery holdover check

1. Set a correct UTC epoch and confirm a passing test.
2. Remove USB and main VCC while leaving the backup coin cell installed.
3. Wait at least a minute, reconnect, and rerun the diagnostic.
4. Confirm `lost_power=false` and that UTC advanced by the power-off duration.

If `lost_power=true`, check the coin-cell presence, polarity, voltage, and the
module's battery circuit. Never use a rechargeable LIR2032 in a holder intended
for a non-rechargeable CR2032, or vice versa; follow the module's schematic.

