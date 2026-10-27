#pragma once

#include <Arduino.h>

namespace config {

// One shared I2C bus. The 3.3 V sensors connect directly to the low-voltage
// D1/D2 side; the 5 V ADS1115 remains behind the bidirectional level shifter.
constexpr uint8_t I2C_SCL_PIN = D1;  // GPIO5, clock
constexpr uint8_t I2C_SDA_PIN = D2;  // GPIO4, data
constexpr uint8_t ONE_WIRE_PIN = D5;      // GPIO14, 4.7 kOhm pull-up to 3.3 V
constexpr uint8_t GPS_RX_PIN = D6;        // GPIO12 <- level shifter <- GPS TX
constexpr int8_t GPS_TX_PIN = -1;         // GPS RX is intentionally disconnected

constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint8_t ADS1115_ADDRESS = 0x48;
constexpr float ADS1115_LSB_MV = 0.1875f;  // GAIN_TWOTHIRDS, +/-6.144 V
constexpr int16_t ADS1115_MIN_COUNTS = 0;
// With VDD=5 V, the input must not exceed VDD even though the PGA scale is
// +/-6.144 V. 5.0 V / 0.1875 mV is approximately 26667 counts.
constexpr int16_t ADS1115_MAX_COUNTS = 26667;
constexpr uint8_t ADS1115_MEDIAN_SAMPLES = 9;
constexpr uint8_t SOIL_DEPTH_CM[2] = {15, 45};

// Devices on the shared D1/D2 I2C bus.
constexpr uint8_t SHT45_ADDRESS = 0x44;
// TSL2584TSV ADDR_SEL: GND -> 0x29, VDD -> 0x49.
constexpr uint8_t TSL2584_SEA_ADDRESS = 0x29;
constexpr uint8_t TSL2584_LAND_ADDRESS = 0x49;
constexpr uint8_t TCS3448_ADDRESS = 0x59;

// Fixed physical DS18B20 slots. Match the complete ROM so discovery order or
// a missing probe never renumbers installed sensors.
constexpr uint8_t DS18B20_COUNT = 4;
constexpr uint8_t DS18B20_ROMS[DS18B20_COUNT][8] = {
    {0x28, 0x8C, 0x18, 0x6F, 0x00, 0x00, 0x00, 0xC1},
    {0x28, 0x8A, 0x10, 0xCC, 0x00, 0x00, 0x00, 0x6C},
    {0x28, 0x41, 0x32, 0x6E, 0x00, 0x00, 0x00, 0xDE},
    {0x28, 0x6B, 0x21, 0x67, 0x00, 0x00, 0x00, 0x06},
};
constexpr uint8_t DS18B20_DEPTH_CM[DS18B20_COUNT] = {5, 15, 30, 45};

constexpr uint32_t GPS_SERIAL_BAUD = 9600;
constexpr int GPS_RX_BUFFER_BYTES = 256;
constexpr int GPS_ISR_BUFFER_WORDS = 1024;
constexpr uint32_t GPS_MAX_FIX_AGE_MS = 3000;
constexpr uint32_t GPS_MAX_TIME_AGE_MS = 3000;
constexpr uint32_t GPS_PRESENT_MAX_AGE_MS = 10000;
constexpr uint8_t GPS_MIN_SATELLITES = 3;
constexpr uint32_t GPS_DISCIPLINE_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t GPS_TIME_HOLDOVER_DELAY_MS = 3000;
constexpr uint16_t DS18B20_CONVERSION_MS = 750;
constexpr uint32_t SENSOR_RECOVERY_INTERVAL_MS = 30000;

constexpr uint32_t SAMPLE_INTERVAL_MS = 60000;
constexpr uint8_t OFFLINE_REPLAY_BATCH_SIZE = 2;
constexpr uint32_t OFFLINE_REPLAY_RETRY_MS = 2000;
constexpr uint32_t OFFLINE_REPLAY_SAMPLE_GUARD_MS = 15000;

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
constexpr char FIRMWARE_VERSION[] = "0.10.0";
constexpr char INGEST_URL[] = "https://ingest.turtleguard.in/v1/readings";
constexpr char BATCH_INGEST_URL[] =
    "https://ingest.turtleguard.in/v1/readings/batch";

// Calibrate each LM393 probe/module pair with dry and wet raw ADS1115 counts.
// Equal values intentionally leave moisture_pct null.
constexpr int16_t SOIL_DRY_COUNTS[2] = {0, 0};
constexpr int16_t SOIL_WET_COUNTS[2] = {0, 0};

}  // namespace config
