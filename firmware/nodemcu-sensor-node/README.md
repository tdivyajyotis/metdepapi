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

The NodeMCU/Uno UART uses a newline-framed `R` command at 57600 baud. The
NodeMCU retries a request when no response begins, accepts the Uno's JSON only
after full validation, and preserves malformed/partial-response details in
telemetry for diagnosis. The Uno continues to accept the legacy `READ` command.

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
| TSL2584 sea-facing | `0x29` | ADDR_SEL to GND; payload key `tsl2584_sea` |
| TSL2584 land-facing | `0x49` | ADDR_SEL to VDD; payload key `tsl2584_land` |
| DS3231 RTC | `0x68` | Shared I2C bus; the NodeMCU is its only controller |

The four temperature probes are assigned by their complete 64-bit ROM, never
by discovery order. They remain in these slots even when one is disconnected:

| Sensor | ROM | Last byte | Depth below sand surface |
| --- | --- | --- | --- |
| 1 | `288C186F000000C1` | `C1` | 5 cm |
| 2 | `288A10CC0000006C` | `6C` | 15 cm |
| 3 | `2841326E000000DE` | `DE` | 30 cm |
| 4 | `286B216700000006` | `06` | 45 cm |

The payload includes `sensor_id`, full `rom`, `rom_suffix`, and `depth_cm` for
every slot. A missing probe is still emitted in its assigned slot with
`present: false` and `ok: false`.

The soil modules have `VCC`, `GND`, `AO`, and `DO`. Connect `AO` to Uno A0/A1
and leave `DO` disconnected; the digital comparator discards most of the useful
moisture information. Ensure the analog outputs remain within the Uno's selected
ADC reference voltage. Add local decoupling
near the LM393 boards because the 1.3 m probe cables can pick up noise.

Soil channels are also fixed: Uno `A0` is soil sensor 1 at 15 cm, and `A1` is
soil sensor 2 at 45 cm. These defaults live in `SOIL_DEPTH_CM` in NodeMCU
`include/config.h`; each reading carries `sensor_id`, `uno_pin`, and `depth_cm`.

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
4. Fill in the WiFi and device-token values.
5. Edit `include/config.h` for the endpoint, device ID, interval, and pins.
6. Connect the NodeMCU and run `pio run -t upload`, followed by
   `pio run -t uploadfs`. The second command installs the tracked Mozilla CA
   store into LittleFS.
7. Open the serial monitor with `pio device monitor`. Confirm that startup
   reports a nonzero number of trust anchors loaded from LittleFS.

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
timezone-aware IST (`+05:30`) timestamp once RTC, NTP, or GPS has provided
valid time. Unix epochs remain UTC-based, so storage, ordering, and certificate
validation use the correct instant. The server should enforce uniqueness on
`event_id` so retries are idempotent.

Each request also carries a bounded, read-only `telemetry` snapshot: NodeMCU
reset/memory state, WiFi, TLS trust-store state, I2C scan results, sensor
presence, Uno-link health, the Uno's own counters and memory state, HTTP upload
counters, clock state, and the twelve most recent structured diagnostic events.
The server dashboard renders this as a serial-like console and refreshes every
30 seconds. It intentionally provides no command route back to either MCU, and
credentials are never included in telemetry.

Sampling starts only after hardware discovery and initialization have finished.
It continues once per minute without WiFi, using RTC/GPS/system time when
available. A live reading is sent immediately when the queue is empty and the
HTTPS prerequisites are ready; otherwise it is appended to the flash queue.

TSL2584 values are deliberately sent as raw broadband, infrared, and derived
visible counts. Converting them to calibrated lux depends on the optical stack
and should be done after calibration rather than applying a misleading generic
constant. The `0x29` device is emitted as `tsl2584_sea`; the `0x49` device is
emitted as `tsl2584_land`.

LittleFS contains a 512 KiB ring of 2,048 fixed 256-byte, versioned and
checksummed records. That provides about 34 hours at the one-minute interval,
including more than the required 24-hour outage. The queue is scanned after a
restart, partially written records are ignored, and an acknowledged record is
invalidated only after the API returns success. When full, the oldest record is
discarded and the drop counter is exposed in telemetry.

After WiFi returns, the node replays two readings per bounded batch request.
Sampling has priority: replay stops 15 seconds before the next measurement,
stores that new measurement, then resumes. Normal live requests retain full
diagnostic telemetry. Historical replay requests retain the original event ID,
sequence, observation time and sensor values, and add `telemetry.delivery`
fields describing flash storage, queue delay and queue depth. Full live
telemetry exposes `offline_queue` readiness, capacity, depth, replay/drop
counters and corrupt-record count.

## Clock-source priority

At boot, a valid DS3231 restores the system clock immediately. SNTP then runs
until its first successful synchronization, writes that NTP time to the
DS3231, labels the active source `ntp`, and stops. When the Uno later relays
fresh GPS time, the NodeMCU switches to GPS, writes the GPS time to the DS3231,
and labels the source `gps`. A position fix is not required for clock
discipline: checksum-valid NMEA time remains usable while coordinates stay
invalid and are omitted. Loss of fresh GPS time becomes `gps_holdover`; the
local clock continues from its most recent discipline.

`sensors.timekeeping` reports `source`, `rtc_available`, UTC and IST strings,
Unix epochs, and `rtc_last_set_source`. Telemetry also reports whether NTP was
applied and whether the latest GPS fix and time are fresh. Source-transition
events distinguish a normal RTC-to-NTP-to-GPS handoff from a wall-clock jump.
A DS3231 stores time but not provenance, so after a reboot an RTC-restored clock
is conservatively labeled `rtc` until NTP or GPS refreshes it. `sensors.gps`
reports the Uno-relayed fix, location, UTC time, and IST time.

HTTPS is attempted only after the clock is plausible. The firmware uses the
Mozilla website-trust roots in `data/certs.ar` through BearSSL's flash-backed
`CertStore`; only the root needed for a connection is loaded into RAM. If
LittleFS is missing or corrupt, it fails over to the compiled GTS Root R4 that
validates the Cloudflare WE1 chain in use when this release was built. Keep
`ALLOW_INSECURE_TLS` set to `0`. `TLS_ROOT_CA_PEM_OVERRIDE` in `secrets.h`
changes only that compiled fallback.

The CA archive is checked in so ordinary builds do not depend on the network.
Its source URL, generation time, hashes, and certificate inventory are recorded
in `data/ca-bundle-manifest.json`. Refresh it periodically from Mozilla's CCADB
report, review the manifest diff, rebuild, and upload the filesystem image:

```powershell
python tools/generate_ca_bundle.py
pio run -t buildfs
pio run -t uploadfs
```

The generator keeps only currently usable website-trust roots, verifies every
reported SHA-256 fingerprint, writes a deterministic Unix archive, and verifies
that archive before replacing the bundle. Updating the application alone does
not update LittleFS, so deployments that change the CA bundle must include
`uploadfs`. Uploading a filesystem image replaces the LittleFS partition and
therefore clears any locally queued readings; do it only after the queue has
drained or when intentional data loss is acceptable.
