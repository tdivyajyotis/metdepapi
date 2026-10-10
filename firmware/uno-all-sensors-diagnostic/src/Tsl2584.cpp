#include "Tsl2584.h"

namespace {
constexpr uint8_t COMMAND = 0x80;
constexpr uint8_t COMMAND_AUTOINCREMENT = 0xA0;
constexpr uint8_t REG_CONTROL = 0x00;
constexpr uint8_t REG_TIMING = 0x01;
constexpr uint8_t REG_ANALOG = 0x07;
constexpr uint8_t REG_ID = 0x12;
constexpr uint8_t REG_DATA0_LOW = 0x14;

constexpr uint8_t CONTROL_POWER = 0x01;
constexpr uint8_t CONTROL_ADC_ENABLE = 0x02;
constexpr uint8_t CONTROL_ADC_VALID = 0x10;

// These match the production NodeMCU driver: nominal 100 ms integration and
// 16x analog gain.
constexpr uint8_t TIMING_100_MS = 0xDB;
constexpr uint8_t GAIN_16X = 0x02;
}  // namespace

bool Tsl2584::begin(TwoWire &wire) {
  wire_ = &wire;

  uint8_t id = 0;
  if (!readRegister(REG_ID, id) || (id & 0xF0) != 0x90) {
    return false;
  }
  if (!writeRegister(REG_CONTROL, CONTROL_POWER)) {
    return false;
  }
  delay(3);
  if (!writeRegister(REG_TIMING, TIMING_100_MS) ||
      !writeRegister(REG_ANALOG, GAIN_16X)) {
    return false;
  }
  return writeRegister(REG_CONTROL, CONTROL_POWER | CONTROL_ADC_ENABLE);
}

bool Tsl2584::read(Tsl2584Reading &reading) {
  uint8_t control = 0;
  if (!readRegister(REG_CONTROL, control) ||
      (control & CONTROL_ADC_VALID) == 0) {
    return false;
  }

  uint8_t data[4] = {};
  if (!readRegisters(REG_DATA0_LOW, data, sizeof(data))) {
    return false;
  }
  reading.broadbandCounts = static_cast<uint16_t>(data[0]) |
                            (static_cast<uint16_t>(data[1]) << 8);
  reading.infraredCounts = static_cast<uint16_t>(data[2]) |
                           (static_cast<uint16_t>(data[3]) << 8);
  reading.visibleCounts = reading.broadbandCounts > reading.infraredCounts
                              ? reading.broadbandCounts - reading.infraredCounts
                              : 0;
  reading.saturated = reading.broadbandCounts == 0xFFFF ||
                      reading.infraredCounts == 0xFFFF;
  return true;
}

bool Tsl2584::writeRegister(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(COMMAND | (reg & 0x1F));
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Tsl2584::readRegister(uint8_t reg, uint8_t &value) {
  return readRegisters(reg, &value, 1);
}

bool Tsl2584::readRegisters(uint8_t startReg, uint8_t *data, size_t length) {
  wire_->beginTransmission(address_);
  wire_->write((length > 1 ? COMMAND_AUTOINCREMENT : COMMAND) |
               (startReg & 0x1F));
  if (wire_->endTransmission(false) != 0) {
    return false;
  }
  const size_t received =
      wire_->requestFrom(address_, static_cast<uint8_t>(length));
  if (received != length) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    data[index] = wire_->read();
  }
  return true;
}
