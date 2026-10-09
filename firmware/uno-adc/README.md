# Arduino Uno ADC coprocessor

This firmware replaces the ADS1115 previously used by the NodeMCU sensor
node. The Uno samples two analog soil-moisture inputs, applies a nine-sample
median filter, and returns a compact JSON record when the NodeMCU sends
`READ` followed by a newline. It also continuously parses NMEA from the GPS
and relays GPS date/time, fix, position, satellites, HDOP, and altitude in the
same response. The NodeMCU never connects directly to the GPS.

GPS time and navigation fix are deliberately independent. TinyGPS++ accepts
date/time from checksum-valid RMC and time from checksum-valid GGA even when
RMC status is `V` or GGA fix quality is `0`. In that state the Uno relays
`time_valid: true` and `fix_valid: false`; coordinates are omitted. Raw NMEA
may still contain latitude/longitude fields in a no-fix sentence, but those
fields are not treated as a valid position.

Both ends resynchronize the newline-framed command at startup. A partial byte
seen while either MCU is booting is discarded, and the NodeMCU keeps listening
past a stale error line for the valid `READ` response.

The response also contains a `telemetry` object with firmware and uptime,
available SRAM, reset flags, command/overflow/sample/loop counters, TinyGPS
parser counters, and soil-power-switch configuration. The NodeMCU forwards
that snapshot to the server; there is no server-to-Uno command path.

## Wiring

| Signal | Arduino Uno | NodeMCU ESP8266 | Notes |
| --- | --- | --- | --- |
| Serial from Uno | D11 / TX | D6 / RX | **Use a 5 V-to-3.3 V level shifter or resistor divider** |
| Serial to Uno | D10 / RX | D7 / TX | The NodeMCU's 3.3 V HIGH is accepted by the Uno |
| Ground | GND | GND | A common ground is required |
| Soil sensor 1 | A0 | - | Analog output must remain between 0 V and the Uno ADC reference |
| Soil sensor 2 | A1 | - | Analog output must remain between 0 V and the Uno ADC reference |
| GPS TX | D8 / AltSoftSerial RX | - | NMEA at 9600 baud; GPS TX must be electrically safe for the Uno |
| GPS RX | D9 / AltSoftSerial TX | - | Optional; leave disconnected when the station never configures the GPS |

The station mapping is deterministic: `A0` is soil sensor 1 at 15 cm and `A1`
is soil sensor 2 at 45 cm. Depth metadata is attached by the NodeMCU from its
`SOIL_DEPTH_CM` configuration.

For a resistor divider on the Uno-to-NodeMCU line, connect Uno D11 through
1 kOhm to NodeMCU D6 and connect 2 kOhm from NodeMCU D6 to ground. Do not
connect the Uno's 5 V TX directly to an ESP8266 input.

The default ADC reference is the Uno's supply voltage. `config.h` therefore
assumes that A0 and A1 never exceed that supply. The NodeMCU uses 5.0 V when
reporting approximate `voltage_v`; measure the Uno's actual 5 V rail and
update `UNO_ADC_REFERENCE_V` in the NodeMCU configuration when voltage
accuracy matters. Soil calibration should use `raw_counts`, not voltage.

AltSoftSerial is used for GPS so NMEA reception can continue while the
SoftwareSerial link on D10/D11 handles NodeMCU commands. On an Arduino Uno,
AltSoftSerial fixes RX to D8 and TX to D9.

## Build and upload

```powershell
pio run
pio run -t upload
pio device monitor
```

If the soil modules are switched through a MOSFET or load switch, set
`SOIL_POWER_PIN` in `include/config.h`. Do not power both modules directly
from an Uno GPIO.
