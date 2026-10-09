#pragma once

#include <Arduino.h>

namespace config {

constexpr char FIRMWARE_VERSION[] = "0.2.2";

constexpr uint8_t NODEMCU_RX_PIN = 10;
constexpr uint8_t NODEMCU_TX_PIN = 11;
constexpr uint32_t NODEMCU_SERIAL_BAUD = 9600;

// AltSoftSerial uses fixed pins on the Arduino Uno: RX=8 and TX=9. Only the
// GPS TX -> Uno pin 8 connection is required. Keep GPS logic at a safe level
// for the Uno/GPS modules in use.
constexpr uint8_t GPS_RX_PIN = 8;
constexpr uint8_t GPS_TX_PIN = 9;
constexpr uint32_t GPS_SERIAL_BAUD = 9600;
// NMEA navigation validity and UTC validity are independent. RMC with status
// V and GGA with fix quality 0 may still carry valid UTC fields, so do not
// require a position fix before relaying fresh GPS date/time.
constexpr uint32_t GPS_MAX_POSITION_AGE_MS = 3000;
constexpr uint32_t GPS_MAX_TIME_AGE_MS = 3000;
constexpr uint32_t GPS_MAX_DATE_AGE_MS = 3000;

constexpr uint8_t SOIL_ANALOG_PINS[2] = {A0, A1};
constexpr uint8_t SOIL_POWER_PIN = 0xFF;
constexpr uint8_t SOIL_POWER_ACTIVE_LEVEL = HIGH;
constexpr uint16_t SOIL_POWER_SETTLE_MS = 300;
constexpr uint8_t MEDIAN_SAMPLE_COUNT = 9;
constexpr uint16_t SAMPLE_GAP_MS = 8;

}  // namespace config
