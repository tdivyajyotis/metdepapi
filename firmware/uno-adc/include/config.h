#pragma once

#include <Arduino.h>

namespace config {

constexpr uint8_t NODEMCU_RX_PIN = 10;
constexpr uint8_t NODEMCU_TX_PIN = 11;
constexpr uint32_t NODEMCU_SERIAL_BAUD = 9600;

constexpr uint8_t SOIL_ANALOG_PINS[2] = {A0, A1};
constexpr uint8_t SOIL_POWER_PIN = 0xFF;
constexpr uint8_t SOIL_POWER_ACTIVE_LEVEL = HIGH;
constexpr uint16_t SOIL_POWER_SETTLE_MS = 300;
constexpr uint8_t MEDIAN_SAMPLE_COUNT = 9;
constexpr uint16_t SAMPLE_GAP_MS = 8;

}  // namespace config
