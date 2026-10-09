#include <Arduino.h>
#include <AltSoftSerial.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <avr/io.h>
#include <string.h>

#include "config.h"

extern "C" {
extern char __heap_start;
extern void *__brkval;
}

namespace {

SoftwareSerial nodeMcuSerial(config::NODEMCU_RX_PIN, config::NODEMCU_TX_PIN);
AltSoftSerial gpsSerial;
TinyGPSPlus gps;
uint32_t sequenceNumber = 0;
uint32_t commandsReceived = 0;
uint32_t unknownCommands = 0;
uint32_t commandOverflows = 0;
uint32_t adcSamplesCompleted = 0;
uint32_t loopCount = 0;
uint32_t lastSampleAt = 0;
uint8_t resetFlags = 0;
char commandBuffer[16] = {};
uint8_t commandLength = 0;

int freeSramBytes() {
  char stackTop = 0;
  const char *heapTop = __brkval == nullptr
                            ? &__heap_start
                            : static_cast<const char *>(__brkval);
  return &stackTop - heapTop;
}

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
  ++adcSamplesCompleted;
  lastSampleAt = millis();

  nodeMcuSerial.print(F("{\"v\":1,\"seq\":"));
  nodeMcuSerial.print(sequenceNumber);
  nodeMcuSerial.print(F(",\"a0\":"));
  nodeMcuSerial.print(a0);
  nodeMcuSerial.print(F(",\"a1\":"));
  nodeMcuSerial.print(a1);
  nodeMcuSerial.print(F(",\"gps\":{\"fix_valid\":"));
  const bool fixValid = gps.location.isValid() &&
                        gps.location.age() <= config::GPS_MAX_FIX_AGE_MS;
  const bool timeValid = gps.date.isValid() && gps.time.isValid() &&
                         gps.date.age() <= config::GPS_MAX_FIX_AGE_MS &&
                         gps.time.age() <= config::GPS_MAX_FIX_AGE_MS;
  nodeMcuSerial.print(fixValid ? F("true") : F("false"));
  nodeMcuSerial.print(F(",\"time_valid\":"));
  nodeMcuSerial.print(timeValid ? F("true") : F("false"));
  nodeMcuSerial.print(F(",\"satellites\":"));
  nodeMcuSerial.print(gps.satellites.isValid() ? gps.satellites.value() : 0);
  nodeMcuSerial.print(F(",\"fix_age_ms\":"));
  nodeMcuSerial.print(fixValid ? gps.location.age() : UINT32_MAX);
  if (timeValid) {
    nodeMcuSerial.print(F(",\"year\":"));
    nodeMcuSerial.print(gps.date.year());
    nodeMcuSerial.print(F(",\"month\":"));
    nodeMcuSerial.print(gps.date.month());
    nodeMcuSerial.print(F(",\"day\":"));
    nodeMcuSerial.print(gps.date.day());
    nodeMcuSerial.print(F(",\"hour\":"));
    nodeMcuSerial.print(gps.time.hour());
    nodeMcuSerial.print(F(",\"minute\":"));
    nodeMcuSerial.print(gps.time.minute());
    nodeMcuSerial.print(F(",\"second\":"));
    nodeMcuSerial.print(gps.time.second());
  }
  if (fixValid) {
    nodeMcuSerial.print(F(",\"latitude_deg\":"));
    nodeMcuSerial.print(gps.location.lat(), 6);
    nodeMcuSerial.print(F(",\"longitude_deg\":"));
    nodeMcuSerial.print(gps.location.lng(), 6);
    if (gps.altitude.isValid()) {
      nodeMcuSerial.print(F(",\"altitude_m\":"));
      nodeMcuSerial.print(gps.altitude.meters(), 2);
    }
    if (gps.hdop.isValid()) {
      nodeMcuSerial.print(F(",\"hdop\":"));
      nodeMcuSerial.print(gps.hdop.hdop(), 2);
    }
  }
  nodeMcuSerial.print(F("},\"telemetry\":{\"firmware\":\""));
  nodeMcuSerial.print(config::FIRMWARE_VERSION);
  nodeMcuSerial.print(F("\",\"uptime_ms\":"));
  nodeMcuSerial.print(millis());
  nodeMcuSerial.print(F(",\"free_sram_bytes\":"));
  nodeMcuSerial.print(freeSramBytes());
  nodeMcuSerial.print(F(",\"reset_flags\":"));
  nodeMcuSerial.print(resetFlags);
  nodeMcuSerial.print(F(",\"commands_received\":"));
  nodeMcuSerial.print(commandsReceived);
  nodeMcuSerial.print(F(",\"unknown_commands\":"));
  nodeMcuSerial.print(unknownCommands);
  nodeMcuSerial.print(F(",\"command_overflows\":"));
  nodeMcuSerial.print(commandOverflows);
  nodeMcuSerial.print(F(",\"adc_samples_completed\":"));
  nodeMcuSerial.print(adcSamplesCompleted);
  nodeMcuSerial.print(F(",\"last_sample_ms\":"));
  nodeMcuSerial.print(lastSampleAt);
  nodeMcuSerial.print(F(",\"loop_count\":"));
  nodeMcuSerial.print(loopCount);
  nodeMcuSerial.print(F(",\"gps_chars_processed\":"));
  nodeMcuSerial.print(gps.charsProcessed());
  nodeMcuSerial.print(F(",\"gps_sentences_ok\":"));
  nodeMcuSerial.print(gps.passedChecksum());
  nodeMcuSerial.print(F(",\"gps_checksum_failures\":"));
  nodeMcuSerial.print(gps.failedChecksum());
  nodeMcuSerial.print(F(",\"soil_power_switched\":"));
  nodeMcuSerial.print(config::SOIL_POWER_PIN != 0xFF ? F("true") : F("false"));
  nodeMcuSerial.print(F("}"));
  nodeMcuSerial.print(F("}\n"));

  Serial.print(F("Sample "));
  Serial.print(sequenceNumber);
  Serial.print(F(": A0="));
  Serial.print(a0);
  Serial.print(F(" A1="));
  Serial.println(a1);
}

void handleCommand(const char *command) {
  if (command[0] != '\0') {
    ++commandsReceived;
  }
  if (strcmp(command, "READ") == 0) {
    sendReadings();
  } else if (command[0] != '\0') {
    ++unknownCommands;
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
      // A SoftwareSerial receiver can see a partial byte while either MCU is
      // starting. READ has only one 'R', so it is a safe framing marker that
      // discards any partial command already in progress.
      if (value == 'R') {
        commandLength = 0;
      }
      if (commandLength < static_cast<uint8_t>(sizeof(commandBuffer) - 1)) {
        commandBuffer[commandLength++] = value;
      } else {
        commandLength = 0;
        ++commandOverflows;
      }
    }
  }
}

void receiveGps() {
  while (gpsSerial.available() > 0) {
    gps.encode(static_cast<char>(gpsSerial.read()));
  }
}

}  // namespace

void setup() {
  resetFlags = MCUSR;
  MCUSR = 0;
  Serial.begin(115200);
  nodeMcuSerial.begin(config::NODEMCU_SERIAL_BAUD);
  gpsSerial.begin(config::GPS_SERIAL_BAUD);
  analogReference(DEFAULT);

  if (config::SOIL_POWER_PIN != 0xFF) {
    pinMode(config::SOIL_POWER_PIN, OUTPUT);
    setSoilPower(false);
  }

  Serial.print(F("Arduino Uno ADC coprocessor "));
  Serial.print(config::FIRMWARE_VERSION);
  Serial.print(F(" ready; reset_flags=0x"));
  Serial.println(resetFlags, HEX);
}

void loop() {
  ++loopCount;
  receiveGps();
  receiveCommands();
}
