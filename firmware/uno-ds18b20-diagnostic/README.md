# Arduino Uno DS18B20 diagnostic

This standalone sketch identifies one DS18B20 probe at a time and continuously
prints its temperature. It prints the complete 64-bit ROM address in both a
label-friendly form and a C/C++ byte-array form.

## Wiring

| DS18B20 connection | Arduino Uno |
| --- | --- |
| VCC | 5 V |
| GND | GND |
| DATA | D2 |

Add a **4.7 kOhm resistor between D2/DATA and 5 V**. Use the probe or module
manufacturer's pinout; cable colours on waterproof probes are not universal.
Connect only one probe while recording its identity.

## Build, upload, and monitor

Close any existing serial monitor, replace `COM12` if the spare Uno uses a
different port, and run:

```powershell
cd C:\Users\USER\Documents\ChatGPT\imd\firmware\uno-ds18b20-diagnostic
pio device list
pio run
pio run -t upload --upload-port COM12
pio device monitor --port COM12 --baud 115200
```

After recording and labeling a probe, disconnect power or unplug USB, replace
the probe, restore power, and record the next address. The sketch also rescans
automatically after a disconnection.

Example output:

```text
Devices found: 1
Power mode:    externally powered
Device 1:
  ROM address: 28-8C-18-6F-00-00-00-C1
  C array:     {0x28, 0x8C, 0x18, 0x6F, 0x00, 0x00, 0x00, 0xC1}
  Family:      DS18B20 (0x28)
  ROM CRC:     valid
  Resolution:  12 bits
ROM 28-8C-18-6F-00-00-00-C1  Temperature: 22.3125 degC / 72.1625 degF
```
