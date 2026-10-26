# Sharing GPS time and location with a camera device

## Current station topology

The NEO-6M TX signal enters the NodeMCU directly on D6/GPIO12 through channel
3 of the 5 V-to-3.3 V level shifter. The NodeMCU parses NMEA with TinyGPS++.
NTP supplies initial time when Wi-Fi is available; fresh GPS date/time can then
discipline the system clock without requiring a valid position fix.

There is no external real-time clock. Clock holdover lasts only while the
NodeMCU remains powered.

```text
NEO-6M TX -> level shifter H3/L3 -> NodeMCU D6/GPIO12
NodeMCU   -> server: environmental readings + time/location
camera    -> server: image + capture metadata
```

## Recommended sharing method

Do not connect another controller's UART TX to the GPS TX signal. A GPS UART TX
can normally feed two high-impedance RX inputs if its logic level, cable length,
and fan-out are valid for both devices, but a local transport from the NodeMCU
is easier to validate and gives one owner for freshness and validity rules.

Choose one of these transports after the camera controller is known:

- ESP-NOW when both controllers support it and must work without an access
  point;
- UDP when both devices are always on the same Wi-Fi network;
- a dedicated UART for controllers in the same enclosure; or
- server association for loose timestamping where exposure-level alignment is
  unnecessary.

Every message should include a sequence number, UTC time, time source, validity,
last-GPS-byte age, fix age, and position validity. The camera should reject stale
messages and detect skipped sequence numbers.

```json
{
  "schema_version": 1,
  "source_device": "station-001",
  "sequence": 1842,
  "unix_time_ms": 1791225751123,
  "time_source": "gps",
  "time_valid": true,
  "gps_last_rx_age_ms": 120,
  "latitude_deg": 19.076,
  "longitude_deg": 72.8777,
  "gps_fix_valid": true,
  "gps_fix_age_ms": 180,
  "satellites": 9,
  "hdop": 0.9
}
```

## Decisions still required

- camera controller model;
- whether the GPS breakout exposes PPS;
- required capture-time accuracy;
- physical distance between controllers; and
- whether synchronization must work without Wi-Fi.
