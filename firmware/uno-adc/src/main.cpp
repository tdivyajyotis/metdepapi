#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>

#include "config.h"

namespace {

SoftwareSerial nodeMcuSerial(config::NODEMCU_RX_PIN, config::NODEMCU_TX_PIN);
uint32_t sequenceNumber = 0;
char commandBuffer[16] = {};
uint8_t commandLength = 0;

void setSoilPower(bool enabled) {
  if (config::SOIL_POWER_PIN == 0xFF) {
    return;
  }
  digitalWrite(config::SOIL_POWER_PIN,
               enabled ? config::SOIL_POWER_ACTIVE_LEVEL
                       : !config::SOIL_POWER_ACTIVE_LEVEL);
}

uint16_t readMedian(uint8_t pin) {
  uint16_t samples[config::MEDIAN_SAMPLE_COUNT] = {};

  // Discard the first conversion after changing ADC channels so the
  // sample-and-hold capacitor can settle on the new input.
  analogRead(pin);
  delayMicroseconds(200);

  for (uint8_t i = 0; i < config::MEDIAN_SAMPLE_COUNT; ++i) {
    samples[i] = analogRead(pin);
    if (i + 1 < config::MEDIAN_SAMPLE_COUNT) {
      delay(config::SAMPLE_GAP_MS);
    }
  }

  for (uint8_t i = 1; i < config::MEDIAN_SAMPLE_COUNT; ++i) {
    const uint16_t value = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > value) {
      samples[j + 1] = samples[j];
      --j;
    }
    samples[j + 1] = value;
  }
  return samples[config::MEDIAN_SAMPLE_COUNT / 2];
}

void sendReadings() {
  setSoilPower(true);
  if (config::SOIL_POWER_PIN != 0xFF) {
    delay(config::SOIL_POWER_SETTLE_MS);
  }

  const uint16_t a0 = readMedian(config::SOIL_ANALOG_PINS[0]);
  const uint16_t a1 = readMedian(config::SOIL_ANALOG_PINS[1]);
  setSoilPower(false);
  ++sequenceNumber;

  nodeMcuSerial.print(F("{\"v\":1,\"seq\":"));
  nodeMcuSerial.print(sequenceNumber);
  nodeMcuSerial.print(F(",\"a0\":"));
  nodeMcuSerial.print(a0);
  nodeMcuSerial.print(F(",\"a1\":"));
  nodeMcuSerial.print(a1);
  nodeMcuSerial.print(F("}\n"));

  Serial.print(F("Sample "));
  Serial.print(sequenceNumber);
  Serial.print(F(": A0="));
  Serial.print(a0);
  Serial.print(F(" A1="));
  Serial.println(a1);
}

void handleCommand(const char *command) {
  if (strcmp(command, "READ") == 0) {
    sendReadings();
  } else if (command[0] != '\0') {
    nodeMcuSerial.print(F("{\"v\":1,\"error\":\"unknown_command\"}\n"));
  }
}

void receiveCommands() {
  while (nodeMcuSerial.available() > 0) {
    const char value = static_cast<char>(nodeMcuSerial.read());
    if (value == '\n') {
      commandBuffer[commandLength] = '\0';
      handleCommand(commandBuffer);
      commandLength = 0;
    } else if (value != '\r') {
      if (commandLength < static_cast<uint8_t>(sizeof(commandBuffer) - 1)) {
        commandBuffer[commandLength++] = value;
      } else {
        commandLength = 0;
      }
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  nodeMcuSerial.begin(config::NODEMCU_SERIAL_BAUD);
  analogReference(DEFAULT);

  if (config::SOIL_POWER_PIN != 0xFF) {
    pinMode(config::SOIL_POWER_PIN, OUTPUT);
    setSoilPower(false);
  }

  Serial.println(F("Arduino Uno ADC coprocessor ready"));
}

void loop() {
  receiveCommands();
}
