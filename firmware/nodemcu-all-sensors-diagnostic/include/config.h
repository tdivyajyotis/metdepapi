#pragma once

#include <Arduino.h>

namespace config {

constexpr char DIAGNOSTIC_VERSION[] = "1.3.0";
constexpr uint32_t SERIAL_BAUD = 115200;

// One shared bus: D1 is clock/SCL and D2 is data/SDA.
constexpr uint8_t I2C_SCL_PIN = D1;  // GPIO5
constexpr uint8_t I2C_SDA_PIN = D2;  // GPIO4
constexpr uint8_t ONE_WIRE_PIN = D5;  // GPIO14
constexpr uint8_t GPS_RX_PIN = D6;    // GPIO12, GPS TX -> NodeMCU RX
constexpr int8_t GPS_TX_PIN = -1;     // Receive-only; GPS RX disconnected
constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint32_t GPS_SERIAL_BAUD = 9600;
constexpr uint32_t GPS_TEST_WINDOW_MS = 3500;
constexpr uint32_t GPS_MAX_FIELD_AGE_MS = 3000;
constexpr int GPS_RX_BUFFER_BYTES = 256;
constexpr int GPS_ISR_BUFFER_WORDS = 1024;

constexpr uint8_t ADS1115_ADDRESS = 0x48;
constexpr float ADS1115_LSB_MV = 0.1875f;  // GAIN_TWOTHIRDS
constexpr int16_t ADS1115_MAX_5V_COUNTS = 26667;
constexpr uint8_t ADS1115_MEDIAN_SAMPLES = 9;
constexpr uint8_t SOIL_DEPTH_CM[2] = {15, 45};

constexpr uint8_t SHT45_ADDRESS = 0x44;
constexpr uint8_t TSL2584_SEA_ADDRESS = 0x29;
constexpr uint8_t TSL2584_LAND_ADDRESS = 0x49;
constexpr uint8_t TCS3448_ADDRESS = 0x59;

constexpr uint8_t DS18B20_COUNT = 4;
constexpr uint8_t DS18B20_ROMS[DS18B20_COUNT][8] = {
    {0x28, 0x8C, 0x18, 0x6F, 0x00, 0x00, 0x00, 0xC1},
    {0x28, 0x8A, 0x10, 0xCC, 0x00, 0x00, 0x00, 0x6C},
    {0x28, 0x41, 0x32, 0x6E, 0x00, 0x00, 0x00, 0xDE},
    {0x28, 0x6B, 0x21, 0x67, 0x00, 0x00, 0x00, 0x06},
};
constexpr uint8_t DS18B20_DEPTH_CM[DS18B20_COUNT] = {5, 15, 30, 45};

}  // namespace config

