# NodeMCU sensor node

ESP8266/NodeMCU firmware for:

- four DS18B20 temperature probes on one 1-Wire bus;
- one SHT45 temperature/humidity sensor;
- one TCS3448 14-channel spectral sensor;
- two TSL2584TSV ambient-light sensors;
- two 1.3 m resistive soil-moisture probe/LM393 modules sampled by an Arduino
  Uno ADC coprocessor;
- GPS time/location parsed and relayed by that Uno;
- DS3231 RTC holdover and synchronization; and
- authenticated JSON ingestion over verified HTTPS.

The two TSL2584TSV sensors use separate hardware-selected I2C addresses so
they can coexist with each other and with the fixed-address TCS3448.

## Wiring

| Device | NodeMCU connection | Notes |
| --- | --- | --- |
| I2C SDA | D2 / GPIO4 | Shared by every I2C device |
| I2C SCL | D1 / GPIO5 | Shared by every I2C device |
| Four DS18B20 data pins | D5 / GPIO14 | One shared bus; add 4.7 kOhm from data to 3.3 V |
| Arduino Uno serial RX | D7 / GPIO13 | Connect to Uno D10 |
| Arduino Uno serial TX | D6 / GPIO12 | From Uno D11 through 5 V-to-3.3 V level conversion |
| SHT45 | `0x44` | Default address |
| TCS3448 | `0x39` | Fixed address |
| TSL2584 #1 | `0x29` | ADDR_SEL to GND |
| TSL2584 #2 | `0x49` | ADDR_SEL to VDD |
| DS3231 RTC | `0x68` | Shared I2C bus; the NodeMCU is its only controller |

The soil modules have `VCC`, `GND`, `AO`, and `DO`. Connect `AO` to Uno A0/A1
and leave `DO` disconnected; the digital comparator discards most of the useful
moisture information. Ensure the analog outputs remain within the Uno's selected
ADC reference voltage. Add local decoupling
near the LM393 boards because the 1.3 m probe cables can pick up noise.

Use a breakout with regulation and level shifting for the TCS3448. The bare
TCS3448 is a 1.8 V part and must not be connected directly to the NodeMCU's
3.3 V I2C bus. The Uno, NodeMCU, and sensors must share ground.

These are resistive two-electrode probes, not capacitive probes. Continuous DC
excitation accelerates polarization and corrosion. For longer life, power both
modules only during a measurement using a MOSFET or load switch controlled by
the Uno firmware's `SOIL_POWER_PIN`. Do **not** power them directly from a GPIO:
the two modules together are specified at roughly 30 mA. Leave that pin at
`0xFF` if they are continuously powered.

Do not use `0x39` for either TSL2584: it conflicts with the TCS3448.

## Build and upload

1. Install VS Code and the PlatformIO extension, or PlatformIO Core.
2. Copy `include/secrets.example.h` to `include/secrets.h`.
3. Build and upload `../uno-adc` to the Uno, then wire the serial link as
   documented in its README.
4. Fill in the WiFi and device-token values. GTS Root R4 for the current
   Cloudflare chain is included in the tracked firmware.
5. Edit `include/config.h` for the endpoint, device ID, interval, and pins.
6. Connect the NodeMCU and run `pio run -t upload`.
7. Open the serial monitor with `pio device monitor`.

At startup, the firmware prints every discovered I2C address and a sensor
presence summary. Missing sensors are represented as `"ok": false`; they do
not stop the rest of the station.

## Soil-moisture calibration

The Uno firmware takes nine analog samples per channel and transmits their median,
which rejects occasional cable and switching noise. Raw 10-bit Uno ADC readings
are always transmitted. To enable `moisture_pct`:

1. Record each probe's 10-bit `raw_counts` while dry/in air.
2. Record it in fully wetted reference soil (not directly in a cup of water).
3. Put those two values into `SOIL_DRY_COUNTS` and `SOIL_WET_COUNTS` in
   `include/config.h`.

Probe/module pairs differ substantially, and soil type, compaction, salinity,
temperature, and insertion depth all affect this resistive measurement. A
factory-generic percentage would therefore be misleading. The code supports
either polarity: wet may be above or below the dry count.

## Payload behavior

Each request includes a unique boot/session event ID and sequence number,
sensor health flags, raw optical/ADC values, WiFi RSSI, firmware version, and a
UTC timestamp once RTC, NTP, or GPS has provided valid time. The server should
enforce uniqueness on `event_id` so retries are idempotent.

TSL2584 values are deliberately sent as raw broadband, infrared, and derived
visible counts. Converting them to calibrated lux depends on the optical stack
and should be done after calibration rather than applying a misleading generic
constant.

The current firmware retries WiFi automatically but does not persist samples
through a power failure. Flash-backed queuing is the next addition if the node
must tolerate long outages without losing readings.

## Clock-source priority

At boot, a valid DS3231 restores the system clock immediately. SNTP then runs
until its first successful synchronization, writes that NTP time to the
DS3231, labels the active source `ntp`, and stops. When the Uno later relays a
fresh GPS time and position fix, the NodeMCU switches to GPS, writes the GPS
time to the DS3231, and labels the source `gps`. Loss of the fix becomes
`gps_holdover`; the local clock continues from its most recent discipline.

`sensors.timekeeping` reports `source`, `rtc_available`, and
`rtc_last_set_source`. A DS3231 stores time but not provenance, so after a
reboot an RTC-restored clock is conservatively labeled `rtc` until NTP or GPS
refreshes it. `sensors.gps` reports the Uno-relayed fix and location.

HTTPS is attempted only after the clock is plausible. The tracked GTS Root R4
trust anchor validates the current Cloudflare WE1 certificate chain. Keep
`ALLOW_INSECURE_TLS` set to `0`; define `TLS_ROOT_CA_PEM_OVERRIDE` in
`secrets.h` only if the public hostname later changes to a different CA.
