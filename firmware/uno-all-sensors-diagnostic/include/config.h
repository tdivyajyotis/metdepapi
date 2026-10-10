#pragma once

#include <Arduino.h>

namespace config {

// Arduino Uno hardware I2C pins are fixed: SDA=A4 and SCL=A5.
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t I2C_CLOCK_HZ = 100000;

constexpr uint8_t SHT45_ADDRESS = 0x44;
constexpr uint8_t TSL2584_SEA_ADDRESS = 0x29;
constexpr uint8_t TSL2584_LAND_ADDRESS = 0x49;
constexpr uint8_t TCS3448_ADDRESS = 0x59;
constexpr uint8_t DS3231_ADDRESS = 0x68;
constexpr uint8_t DS3231_EEPROM_ADDRESS = 0x50;

// These are the same fixed AltSoftSerial pins and freshness limits used by
// the production Uno ADC/GPS firmware. Only GPS TX -> Uno D8 is required.
constexpr bool ENABLE_GPS = true;
constexpr uint8_t GPS_RX_PIN = 8;
constexpr uint8_t GPS_TX_PIN = 9;
constexpr uint32_t GPS_SERIAL_BAUD = 9600;
constexpr uint32_t GPS_TEST_WINDOW_MS = 2500;
constexpr uint32_t GPS_MAX_POSITION_AGE_MS = 3000;
constexpr uint32_t GPS_MAX_TIME_AGE_MS = 3000;
constexpr uint32_t GPS_MAX_DATE_AGE_MS = 3000;

// Set false when the 1-Wire bus is not connected. When true, the diagnostic
// discovers every valid device and also reports the station's deterministic
// sensor/depth mapping.
constexpr bool ENABLE_DS18B20 = true;
constexpr uint8_t ONE_WIRE_PIN = 2;
constexpr uint8_t DS18B20_COUNT = 4;
constexpr uint8_t DS18B20_ROMS[DS18B20_COUNT][8] = {
    {0x28, 0x8C, 0x18, 0x6F, 0x00, 0x00, 0x00, 0xC1},  // Sensor 1
    {0x28, 0x8A, 0x10, 0xCC, 0x00, 0x00, 0x00, 0x6C},  // Sensor 2
    {0x28, 0x41, 0x32, 0x6E, 0x00, 0x00, 0x00, 0xDE},  // Sensor 3
    {0x28, 0x6B, 0x21, 0x67, 0x00, 0x00, 0x00, 0x06},  // Sensor 4
};
constexpr uint8_t DS18B20_DEPTH_CM[DS18B20_COUNT] = {5, 15, 30, 45};

// Optional direct checks for the station's two soil-sensor analog outputs.
constexpr bool ENABLE_ANALOG_INPUTS = true;
constexpr uint8_t SOIL_ANALOG_PINS[2] = {A0, A1};
constexpr uint8_t SOIL_DEPTH_CM[2] = {15, 45};
constexpr float ADC_REFERENCE_V = 5.0f;
constexpr uint8_t ADC_SAMPLE_COUNT = 9;

}  // namespace config
