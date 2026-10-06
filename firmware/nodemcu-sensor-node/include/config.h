#pragma once

#include <Arduino.h>

namespace config {

// NodeMCU ESP8266 pin labels. D1/D2 are the conventional I2C pins.
constexpr uint8_t I2C_SCL_PIN = D1;
constexpr uint8_t I2C_SDA_PIN = D2;
constexpr uint8_t ONE_WIRE_PIN = D5;

// Software UART to the Arduino Uno ADC coprocessor. D6 receives the Uno's
// 5 V TX signal through a level shifter or resistor divider; D7 transmits a
// 3.3 V signal that the Uno accepts as HIGH.
constexpr uint8_t UNO_RX_PIN = D6;
constexpr uint8_t UNO_TX_PIN = D7;
constexpr uint32_t UNO_SERIAL_BAUD = 9600;
constexpr uint16_t UNO_RESPONSE_TIMEOUT_MS = 1500;
constexpr float UNO_ADC_REFERENCE_V = 5.0f;
constexpr uint16_t UNO_ADC_MAX_COUNTS = 1023;

constexpr uint8_t SHT45_ADDRESS = 0x44;

// TSL2584TSV ADDR_SEL: GND -> 0x29, VDD -> 0x49.
// Do not use its floating 0x39 address: TCS3448 also uses 0x39.
constexpr uint8_t TSL2584_1_ADDRESS = 0x29;
constexpr uint8_t TSL2584_2_ADDRESS = 0x49;

constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint32_t SAMPLE_INTERVAL_MS = 60000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 12000;

constexpr char DEVICE_ID[] = "station-001";
constexpr char FIRMWARE_VERSION[] = "0.2.0";

constexpr char INGEST_URL[] = "https://ingest.atldavunit8.org/v1/readings";

// Calibrate each resistive probe/module pair using your own dry-reference and
// wet-reference soil readings. The default values are placeholders and
// intentionally leave moisture_pct as null.
constexpr int16_t SOIL_DRY_COUNTS[2] = {0, 0};
constexpr int16_t SOIL_WET_COUNTS[2] = {0, 0};

}  // namespace config
