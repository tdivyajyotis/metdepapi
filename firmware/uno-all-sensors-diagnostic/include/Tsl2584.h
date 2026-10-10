#pragma once

#include <Arduino.h>
#include <Wire.h>

struct Tsl2584Reading {
  uint16_t broadbandCounts = 0;
  uint16_t infraredCounts = 0;
  uint16_t visibleCounts = 0;
  bool saturated = false;
};

class Tsl2584 {
 public:
  explicit Tsl2584(uint8_t address) : address_(address) {}

  bool begin(TwoWire &wire = Wire);
  bool read(Tsl2584Reading &reading);
  uint8_t address() const { return address_; }

 private:
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegister(uint8_t reg, uint8_t &value);
  bool readRegisters(uint8_t startReg, uint8_t *data, size_t length);

  TwoWire *wire_ = nullptr;
  uint8_t address_;
};

