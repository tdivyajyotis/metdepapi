# NodeMCU complete station diagnostic

This is a standalone, serial-only diagnostic for the current NodeMCU station.
It does not connect to Wi-Fi, set the clock, contact the server, or modify
LittleFS. It prints one complete report after boot, then remains quiet while
continuing to parse GPS. Send a command in the serial monitor to repeat a test.

It uses the same addresses, gains, integration settings, pin assignments, and
fixed DS18B20 mappings as the production `nodemcu-sensor-node` firmware.

## Current wiring

| Function | NodeMCU pin |
| --- | --- |
| Shared I2C SCL/clock | D1 / GPIO5 |
| Shared I2C SDA/data | D2 / GPIO4 |
| DS18B20 data | D5 / GPIO14, external 4.7 kOhm pull-up to 3.3 V |
| GPS input | GPS TX to D6 / GPIO12 |
| ADS1115 soil 1 | A0 at 15 cm |
| ADS1115 soil 2 | A1 at 45 cm |
| USB serial | 115200 baud |

All five I2C devices share D1-SCL/D2-SDA: TSL2584 sea `0x29`, SHT45 `0x44`,
ADS1115 `0x48`, TSL2584 land `0x49`, and TCS3448 `0x59`. Connect the 3.3 V
sensors directly to the low-voltage side. Keep the 5 V ADS1115 behind the
bidirectional level shifter, and never allow 5 V pull-ups on ESP8266 pins.

## Commands

| Command | Action |
| --- | --- |
| `a` or `r` | complete report |
| `i` or `s` | scan the shared D1/D2 bus |
| `e` | SHT45 |
| `l` | both TSL2584 sensors |
| `c` | TCS3448 |
| `d` | DS18B20 bus, ROM mappings, and temperatures |
| `o` | ADS1115 A0/A1 soil inputs |
| `g` | GPS/NMEA test |
| `b` | NodeMCU board/heap/reset information |
| `h` or `?` | help |

The GPS category passes when checksum-valid NMEA arrives. A position fix is
reported separately and is not required for the UART/wiring test. GPS calendar
data is valid only for complete dates from 2021 through 2099; placeholder year
2000, month/day zero, and impossible dates are printed as rejected and never
reported as valid time.

## Build, upload, and monitor

```powershell
cd C:\Users\USER\Documents\ChatGPT\imd\firmware\nodemcu-all-sensors-diagnostic
pio run
pio device list
pio run -t upload --upload-port COM15
pio device monitor --port COM15 --baud 115200
```

Replace `COM15` if `pio device list` reports another port. This temporarily
replaces production firmware. Rebuild and upload `firmware/nodemcu-sensor-node`
when diagnostics are complete; its LittleFS CA bundle remains intact.

