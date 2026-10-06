# Sharing GPS and RTC data with a camera device

## Goal

Give the environmental sensor node and a separate camera controller a common
UTC timebase and location without allowing two microcontrollers to contend for
the same RTC bus.

## Recommended topology

```text
GPS UART TX --------> sensor-node UART RX
GPS PPS ------------+--> sensor-node interrupt input
                    +--> camera interrupt input

RTC I2C ------------> sensor node only

sensor node --------> camera
              ESP-NOW, UDP, or a dedicated UART link

sensor node --------> server: environmental readings + time/location
camera node --------> server: image + capture metadata
```

The sensor node is the time-and-location authority:

1. Use GPS time whenever GPS time is valid.
2. Discipline/update the RTC from GPS after a stable fix.
3. Use the RTC as holdover during startup, indoor operation, or loss of GPS.
4. Send time, position, validity, uncertainty, and sequence information to the
   camera.
5. Use the GPS PPS edge at both controllers when capture timing needs
   millisecond-scale alignment.

## Why the RTC has one owner

A normal RTC such as a DS3231 is an I2C target. Connecting it to two unrelated
I2C controllers creates a multi-controller bus and introduces arbitration,
startup, and fault-recovery problems that typical Arduino libraries do not
handle reliably. Prefer one of these arrangements:

1. Sensor node owns the RTC and forwards time to the camera (recommended).
2. Give each device its own RTC and synchronize both from GPS.
3. Use a purpose-built I2C multiplexer/arbitrator if physical sharing is an
   unavoidable requirement.

Do not simply join two independently configured I2C controller buses.

## Sharing the GPS signals

A GPS module's UART TX may usually feed the RX input of both microcontrollers,
because RX pins are high impedance. Verify voltage levels and fan-out in the
specific module datasheet. Do not electrically join both controller TX outputs
to one GPS RX input.

The PPS signal may feed an interrupt input on both controllers if its voltage,
drive strength, and edge loading are valid for both. Add a buffer when cable
length, input capacitance, or different voltage domains make direct fan-out
unreliable.

## Device-to-device transport

- **ESP-NOW:** preferred when both devices are ESP8266/ESP32 and must work
  without an access point.
- **UDP on the LAN:** convenient when both devices always use the same Wi-Fi
  network. Add sequence numbers, authentication, and periodic repetition.
- **Dedicated UART:** simplest and most deterministic for devices in the same
  enclosure.
- **Server-mediated synchronization:** acceptable for loose timestamping, but
  not for accurately associating a sensor reading with a camera exposure.

## Synchronization message

```json
{
  "schema_version": 1,
  "source_device": "station-001",
  "sequence": 1842,
  "unix_time_ms": 1791225751123,
  "time_source": "gps",
  "time_valid": true,
  "time_uncertainty_ms": 2,
  "pps_sequence": 88210,
  "latitude_deg": 19.076,
  "longitude_deg": 72.8777,
  "altitude_m": 14.2,
  "gps_fix_valid": true,
  "gps_fix_age_ms": 180,
  "satellites": 9,
  "hdop": 0.9
}
```

The camera should reject stale messages, detect skipped sequence numbers, and
retain its last known synchronization state through temporary link outages.

## Camera upload metadata

Each image should include or be accompanied by:

```text
camera_id
capture_id
captured_at_utc
clock_source                 gps | rtc | synchronized | unsynchronized
clock_uncertainty_ms
latitude_deg
longitude_deg
gps_fix_age_ms
time_sync_sequence
environment_sample_event_id
```

`environment_sample_event_id` lets the server associate a photograph with the
nearest complete environmental sample without relying only on timestamps.

## Decisions still required

The implementation and pin allocation depend on:

- GPS module model and whether it exposes PPS;
- RTC model;
- camera controller model (for example ESP32-CAM);
- physical distance between devices;
- whether the installation must synchronize while Wi-Fi is unavailable; and
- required capture-time accuracy.

