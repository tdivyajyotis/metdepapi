# NodeMCU sensor node

Production ESP8266 firmware for the LoLin NodeMCU V3 station. The NodeMCU now
reads every installed sensor directly; the Arduino Uno ADC/GPS coprocessor is
not part of this design.

## Installed hardware

- four fixed-identity DS18B20 probes on the D5/GPIO14 1-Wire bus;
- ADS1115 at `0x48`, powered from 5 V and reached through the I2C level shifter;
- two LM393 soil modules on ADS1115 channels A0 and A1;
- NEO-6M GPS TX through level-shifter channel 3 to D6/GPIO12;
- SHT45 at `0x44` on the shared D1/D2 I2C bus;
- TSL2584 sea-facing at `0x29` and land-facing at `0x49`;
- TCS3448 breakout on the shared D1/D2 I2C bus;
- verified HTTPS ingestion with a persistent LittleFS offline queue.

## Production wiring

| Signal/device | Connection | Electrical side |
| --- | --- | --- |
| Shared I2C SCL | D1/GPIO5 → sensors and L1; H1 → ADS1115 SCL | 3.3 V low / 5 V high |
| Shared I2C SDA | D2/GPIO4 → sensors and L2; H2 → ADS1115 SDA | 3.3 V low / 5 V high |
| DS18B20 DATA | D5/GPIO14 | 4.7 kΩ pull-up to 3.3 V |
| GPS TX | GPS TX → H3; L3 → D6/GPIO12 | 5 V/3.3 V shifted UART |
| GPS RX | disconnected | Not required for NMEA reception |
| ADS1115 A0/A1 | LM393 AO1/AO2 | 0–5 V analog |
| LM393 power | 5 V and common GND | DO pins unused |

The level converter is powered with `HV=5 V`, `LV=3.3 V`, and common ground.
The ADS1115 uses `GAIN_TWOTHIRDS` (±6.144 V scale), but each analog input must
still remain between ground and its 5 V supply. The single I2C bus runs at
100 kHz: D1 is SCL/clock and D2 is SDA/data. SHT45, both TSL2584s, and TCS3448
must connect to the 3.3 V low side only; the ADS1115 remains on the level
shifter's 5 V high side. Never expose an ESP8266 pin or 3.3 V sensor to a 5 V
I2C pull-up. There is no external real-time clock in this firmware.

## DS18B20 identity map

| Sensor | ROM | Depth |
| --- | --- | --- |
| 1 | `288C186F000000C1` | 5 cm |
| 2 | `288A10CC0000006C` | 15 cm |
| 3 | `2841326E000000DE` | 30 cm |
| 4 | `286B216700000006` | 45 cm |

Slots are matched by the complete ROM rather than discovery order. Missing
probes remain in their assigned payload positions with `present: false`.

## Build and upload

1. Install PlatformIO Core or the VS Code PlatformIO extension.
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill in Wi-Fi and
   API credentials.
3. Review device ID, endpoints, sensor ROMs, depths, and soil calibration in
   `include/config.h`.
4. Run `pio run`, then `pio run -t upload`.
5. Run `pio run -t uploadfs` when installing or changing the CA bundle.
6. Run `pio device monitor` at 115200 baud and verify the I2C scan.

The shared D1/D2 bus expects TSL2584 sea `0x29`, SHT45 `0x44`, ADS1115
`0x48`, TSL2584 land `0x49`, and TCS3448 `0x59`. Unexpected or missing
addresses are reported in telemetry and the serial log.

## Soil calibration

The median of nine ADS1115 samples per channel is sent as raw counts and
voltage. To enable `moisture_pct`, place
each LM393 probe/module pair in a repeatable dry reference and wetted reference
soil, then enter those raw counts in `SOIL_DRY_COUNTS` and `SOIL_WET_COUNTS`.
Equal defaults intentionally produce `null` rather than a fabricated value.

The two channels are fixed: A0 is sensor 1 at 15 cm and A1 is sensor 2 at
45 cm. The firmware accepts either calibration polarity.

## Time and GPS

NTP is a mandatory clock gate. The firmware does not capture, queue, replay, or
transmit any reading unless SNTP has successfully initialized the clock and the
current system time remains valid. Checksum-valid
NEO-6M date/time received on D6 may discipline the clock only after that first
NTP synchronization; a position fix is not required for time. Fresh valid
coordinates, satellites, HDOP, and altitude are emitted when available. If GPS
time becomes stale, the source changes to `gps_holdover` and the ESP8266 clock
continues running.

GPS calendar fields are validated independently of TinyGPS++'s validity flag.
Years before 2021—including the NEO-6M placeholder year 2000—month/day zero,
impossible calendar dates, and invalid time fields are rejected and can never
set the system clock.

There is no battery-backed clock. After every cold boot, sampling and HTTPS
wait specifically for a successful NTP synchronization. The ESP8266 system
clock provides holdover only while the board remains powered.

## Payload and offline behavior

Payloads retain the existing sensor keys for DS18B20, SHT45, both TSL2584
sensors, and TCS3448. Soil entries now identify `ads1115_16bit` and
`adc_channel` instead of an Uno pin. GPS reports `connection: direct_uart_d6`.
Telemetry contains `ads1115` and `gps_uart` health blocks and reports the
current time source. The GPS UART block also counts receive-buffer overflows so
missed NMEA bursts are visible remotely. It no longer emits `uno` or
`uno_link`.

Telemetry version 2 reports whether each current I2C read succeeded, its last
successful-read age, accumulated read failures, and the age of the last GPS
byte. Missing I2C devices are retried every 30 seconds, so late power-up no
longer requires a NodeMCU reboot.

LittleFS stores 1,536 checksummed 256-byte version-4 records, providing 25.6
hours at the one-minute interval. Format 4 aligns every multi-byte field and
adds compile-time alignment checks, preventing ESP8266 exception 9 when sensor
values pass through library templates. Its new magic rejects every older
packed layout. The original fixed-size queue allocation is reused; an
incompatible experimental v2 queue file is removed during the upgrade. Let any
important offline queue drain before installing firmware 0.10.0.

Live readings contain the full diagnostic snapshot. Replayed readings preserve
their original event ID, sequence, observation time, and sensor values while
adding delivery and queue-delay metadata.
