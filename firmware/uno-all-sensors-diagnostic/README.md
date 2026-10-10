# Arduino Uno complete station sensor diagnostic

This standalone sketch checks the station sensor hardware without Wi-Fi or the
server. It intentionally uses the same initialization and measurement calls as
the current NodeMCU firmware:

- SHT45 at `0x44`: high precision, heater disabled, `getEvent()`;
- TSL2584 sea at `0x29` and land at `0x49`: 100 ms integration, 16x gain;
- TCS3448 at `0x59`: 64x gain, ATIME 29, ASTEP 599, 18-channel SMUX;
- DS3231 RTC at `0x68`: `begin()`, `lostPower()`, and `now()`;
- DS18B20: 12-bit blocking conversion, ROM CRC and range validation;
- soil outputs: nine-sample median from Uno A0 and A1;
- GPS: TinyGPS++ over AltSoftSerial at 9600 baud, with position and UTC
  validity assessed independently just like the production Uno firmware.

The sketch prints one complete report after boot and then remains quiet while
continuing to parse GPS. It waits for a one-letter serial command and prints
only the newly requested sensor section:

| Command | Output |
| --- | --- |
| `a` or `r` | force another complete report |
| `i` or `s` | I2C scan |
| `t` | DS3231 RTC |
| `e` | SHT45 environment reading |
| `l` | both TSL2584 light sensors |
| `c` | TCS3448 spectral channels |
| `d` | DS18B20 scan and temperatures |
| `o` | soil analog inputs |
| `g` | GPS/NMEA status and current fix/time |
| `h` or `?` | command help |

## Wiring

| Signal | Arduino Uno |
| --- | --- |
| I2C SDA | A4 |
| I2C SCL | A5 |
| DS18B20 DATA | D2, with external 4.7 kOhm pull-up to the 1-Wire supply |
| soil sensor 1 output | A0 (15 cm assignment) |
| soil sensor 2 output | A1 (45 cm assignment) |
| GPS TX | D8 / AltSoftSerial RX |
| GPS RX | D9 / AltSoftSerial TX, optional and normally disconnected |
| serial monitor | USB, 115200 baud |

Do not add 5 V I2C pull-ups to the station bus. The Uno uses open-drain I2C and
the bus must remain pulled up to the voltage supported by all attached sensor
boards (normally 3.3 V in this station). Power each module only at its rated
voltage and share ground.

GPS, DS18B20, and analog checking can be disabled independently in
`include/config.h`. The DS18B20 scanner accepts arbitrary probes and labels the
four known station ROMs with their deterministic sensor IDs and depths. The GPS
test listens for 2.5 seconds and passes when checksum-valid NMEA arrives; lack
of an indoor satellite fix is reported separately and does not fail the serial
hardware test.

## Build, upload, and monitor

```powershell
cd C:\Users\USER\Documents\ChatGPT\imd\firmware\uno-all-sensors-diagnostic
pio run
pio run --target upload --upload-port COM11
pio device monitor --port COM11 --baud 115200
```

Replace `COM11` with the port shown by `pio device list`. This diagnostic
temporarily replaces the normal Uno ADC/GPS firmware; rebuild and upload
`firmware/uno-adc` before returning the board to service.
