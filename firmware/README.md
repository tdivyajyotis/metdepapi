# Firmware projects

`nodemcu-sensor-node` is the production firmware for the current station. It
directly reads the ADS1115 and NEO-6M and retains the SHT45, TCS3448, two
TSL2584 boards, and DS18B20 bus. Time comes from NTP and the direct GPS UART;
there is no external real-time clock in the production hardware or firmware.

The `uno-*` directories are standalone legacy or bench-diagnostic sketches.
They are not part of the deployed hardware stack, are not required to build the
NodeMCU image, and may describe wiring that is intentionally absent from the
current perfboard.

`nodemcu-all-sensors-diagnostic` is the serial-only bench/in-circuit diagnostic
for the current station. It performs no Wi-Fi or server operations and does not
modify the production LittleFS data.
