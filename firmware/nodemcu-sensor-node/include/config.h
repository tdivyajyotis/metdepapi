#pragma once

#include <Arduino.h>

namespace config {

// NodeMCU ESP8266 pin labels. D1/D2 are the conventional I2C pins.
constexpr uint8_t I2C_SCL_PIN = D1;
constexpr uint8_t I2C_SDA_PIN = D2;
constexpr uint8_t ONE_WIRE_PIN = D5;

// Fixed physical DS18B20 slots. Match the complete ROM, not only its final
// CRC byte, so discovery order or a missing probe can never renumber sensors.
constexpr uint8_t DS18B20_COUNT = 4;
constexpr uint8_t DS18B20_ROMS[DS18B20_COUNT][8] = {
    {0x28, 0x8C, 0x18, 0x6F, 0x00, 0x00, 0x00, 0xC1},  // Sensor 1
    {0x28, 0x8A, 0x10, 0xCC, 0x00, 0x00, 0x00, 0x6C},  // Sensor 2
    {0x28, 0x41, 0x32, 0x6E, 0x00, 0x00, 0x00, 0xDE},  // Sensor 3
    {0x28, 0x6B, 0x21, 0x67, 0x00, 0x00, 0x00, 0x06},  // Sensor 4
};
constexpr uint8_t DS18B20_DEPTH_CM[DS18B20_COUNT] = {5, 15, 30, 45};

// Software UART to the Arduino Uno ADC coprocessor. D6 receives the Uno's
// 5 V TX signal through a level shifter or resistor divider; D7 transmits a
// 3.3 V signal that the Uno accepts as HIGH.
constexpr uint8_t UNO_RX_PIN = D6;
constexpr uint8_t UNO_TX_PIN = D7;
constexpr uint32_t UNO_SERIAL_BAUD = 57600;
constexpr uint16_t UNO_RESPONSE_TIMEOUT_MS = 2500;
constexpr uint16_t UNO_REQUEST_RETRY_MS = 400;
constexpr uint16_t UNO_RESPONSE_GAP_TIMEOUT_MS = 300;
constexpr float UNO_ADC_REFERENCE_V = 5.0f;
constexpr uint16_t UNO_ADC_MAX_COUNTS = 1023;
constexpr uint8_t SOIL_DEPTH_CM[2] = {15, 45};  // Uno A0, A1 respectively.

constexpr uint32_t SAMPLE_INTERVAL_MS = 60000;
constexpr uint8_t OFFLINE_REPLAY_BATCH_SIZE = 2;
constexpr uint32_t OFFLINE_REPLAY_RETRY_MS = 2000;
constexpr uint32_t OFFLINE_REPLAY_SAMPLE_GUARD_MS = 15000;

// GPS NMEA is parsed by the Arduino Uno and relayed in the ADC response.
// Position validity is intentionally not required for GPS clock discipline.
constexpr uint32_t GPS_MAX_FIX_AGE_MS = 3000;
constexpr uint32_t GPS_RELAY_STALE_MS = SAMPLE_INTERVAL_MS + 5000;
constexpr uint32_t GPS_TIME_HOLDOVER_DELAY_MS = 3000;
constexpr uint8_t GPS_MIN_SATELLITES = 3;
constexpr uint32_t GPS_DISCIPLINE_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

// DS3231 RTC on the shared I2C bus at 0x68. Missing or invalid hardware is
// reported in telemetry and does not prevent NTP/GPS operation.
constexpr bool RTC_ENABLED = true;

constexpr uint8_t SHT45_ADDRESS = 0x44;

// TSL2584TSV ADDR_SEL: GND -> 0x29, VDD -> 0x49.
// Do not use its floating 0x39 address: TCS3448 also uses 0x39.
constexpr uint8_t TSL2584_SEA_ADDRESS = 0x29;
constexpr uint8_t TSL2584_LAND_ADDRESS = 0x49;

constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t WIFI_STATUS_LOG_INTERVAL_MS = 5000;
constexpr uint32_t HTTP_TIMEOUT_MS = 12000;
constexpr uint16_t TLS_RECEIVE_BUFFER_BYTES = 4096;
constexpr uint16_t TLS_TRANSMIT_BUFFER_BYTES = 512;
constexpr time_t MIN_VALID_UNIX_TIME = 1609459200;  // 2021-01-01 UTC
constexpr int32_t IST_OFFSET_SECONDS = 5 * 60 * 60 + 30 * 60;
constexpr uint8_t TELEMETRY_MAX_EVENTS = 12;
constexpr size_t TELEMETRY_MESSAGE_LENGTH = 88;

constexpr char DEVICE_ID[] = "station-001";
constexpr char FIRMWARE_VERSION[] = "0.6.1";

constexpr char INGEST_URL[] = "https://ingest.turtleguard.in/v1/readings";
constexpr char BATCH_INGEST_URL[] =
    "https://ingest.turtleguard.in/v1/readings/batch";

// Calibrate each resistive probe/module pair using your own dry-reference and
// wet-reference soil readings. The default values are placeholders and
// intentionally leave moisture_pct as null.
constexpr int16_t SOIL_DRY_COUNTS[2] = {0, 0};
constexpr int16_t SOIL_WET_COUNTS[2] = {0, 0};

}  // namespace config
