# Arduino Uno ADC coprocessor

This firmware replaces the ADS1115 previously used by the NodeMCU sensor
node. The Uno samples two analog soil-moisture inputs, applies a nine-sample
median filter, and returns a compact JSON record when the NodeMCU sends
`READ` followed by a newline.

## Wiring

| Signal | Arduino Uno | NodeMCU ESP8266 | Notes |
| --- | --- | --- | --- |
| Serial from Uno | D11 / TX | D6 / RX | **Use a 5 V-to-3.3 V level shifter or resistor divider** |
| Serial to Uno | D10 / RX | D7 / TX | The NodeMCU's 3.3 V HIGH is accepted by the Uno |
| Ground | GND | GND | A common ground is required |
| Soil sensor 1 | A0 | - | Analog output must remain between 0 V and the Uno ADC reference |
| Soil sensor 2 | A1 | - | Analog output must remain between 0 V and the Uno ADC reference |

For a resistor divider on the Uno-to-NodeMCU line, connect Uno D11 through
1 kOhm to NodeMCU D6 and connect 2 kOhm from NodeMCU D6 to ground. Do not
connect the Uno's 5 V TX directly to an ESP8266 input.

The default ADC reference is the Uno's supply voltage. `config.h` therefore
assumes that A0 and A1 never exceed that supply. The NodeMCU uses 5.0 V when
reporting approximate `voltage_v`; measure the Uno's actual 5 V rail and
update `UNO_ADC_REFERENCE_V` in the NodeMCU configuration when voltage
accuracy matters. Soil calibration should use `raw_counts`, not voltage.

## Build and upload

```powershell
pio run
pio run -t upload
pio device monitor
```

If the soil modules are switched through a MOSFET or load switch, set
`SOIL_POWER_PIN` in `include/config.h`. Do not power both modules directly
from an Uno GPIO.
