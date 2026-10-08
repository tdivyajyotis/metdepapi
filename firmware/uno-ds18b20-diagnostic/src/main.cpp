#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

namespace {

constexpr uint8_t ONE_WIRE_PIN = 2;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
DeviceAddress activeAddress = {};
bool activeSensor = false;
uint32_t lastSampleAt = 0;

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printRom(const DeviceAddress address) {
  for (uint8_t index = 0; index < 8; ++index) {
    if (index != 0) {
      Serial.print('-');
    }
    printHexByte(address[index]);
  }
}

void printRomAsCArray(const DeviceAddress address) {
  Serial.print(F("{"));
  for (uint8_t index = 0; index < 8; ++index) {
    if (index != 0) {
      Serial.print(F(", "));
    }
    Serial.print(F("0x"));
    printHexByte(address[index]);
  }
  Serial.print(F("}"));
}

const __FlashStringHelper *familyName(uint8_t family) {
  switch (family) {
    case 0x28:
      return F("DS18B20");
    case 0x10:
      return F("DS18S20");
    case 0x22:
      return F("DS1822");
    default:
      return F("unknown 1-Wire family");
  }
}

bool addressIsValid(const DeviceAddress address) {
  return OneWire::crc8(address, 7) == address[7];
}

void printDevice(uint8_t index, const DeviceAddress address) {
  Serial.print(F("Device "));
  Serial.print(index + 1);
  Serial.println(':');

  Serial.print(F("  ROM address: "));
  printRom(address);
  Serial.println();

  Serial.print(F("  C array:     "));
  printRomAsCArray(address);
  Serial.println();

  Serial.print(F("  Family:      "));
  Serial.print(familyName(address[0]));
  Serial.print(F(" (0x"));
  printHexByte(address[0]);
  Serial.println(')');

  Serial.print(F("  ROM CRC:     "));
  Serial.println(addressIsValid(address) ? F("valid") : F("INVALID"));

  Serial.print(F("  Resolution:  "));
  Serial.print(sensors.getResolution(address));
  Serial.println(F(" bits"));
}

void scanBus() {
  activeSensor = false;
  memset(activeAddress, 0, sizeof(activeAddress));

  // begin() performs a fresh 1-Wire search, so a probe can be swapped without
  // uploading the sketch again.
  sensors.begin();
  const uint8_t count = sensors.getDeviceCount();

  Serial.println();
  Serial.println(F("Scanning the 1-Wire bus..."));
  Serial.print(F("Devices found: "));
  Serial.println(count);
  Serial.print(F("Power mode:    "));
  Serial.println(sensors.isParasitePowerMode() ? F("parasite")
                                                : F("externally powered"));

  if (count == 0) {
    Serial.println(F("No sensor found. Check VCC, GND, DATA, and the 4.7 kOhm pull-up."));
    return;
  }

  for (uint8_t index = 0; index < count; ++index) {
    DeviceAddress address = {};
    if (!sensors.getAddress(address, index)) {
      Serial.print(F("Could not read the address for device index "));
      Serial.println(index);
      continue;
    }
    printDevice(index, address);
    if (index == 0 && addressIsValid(address)) {
      memcpy(activeAddress, address, sizeof(activeAddress));
      activeSensor = true;
    }
  }

  if (count > 1) {
    Serial.println(F("WARNING: connect only one probe when assigning physical labels."));
    activeSensor = false;
  } else if (!activeSensor) {
    Serial.println(F("The discovered ROM failed CRC validation; readings are disabled."));
  } else {
    Serial.println(F("Beginning temperature readings every 2 seconds."));
  }
}

void printTemperature() {
  sensors.requestTemperaturesByAddress(activeAddress);
  const float temperatureC = sensors.getTempC(activeAddress);

  if (temperatureC == DEVICE_DISCONNECTED_C) {
    Serial.println(F("Sensor disconnected or conversion failed; rescanning..."));
    scanBus();
    return;
  }

  Serial.print(F("ROM "));
  printRom(activeAddress);
  Serial.print(F("  Temperature: "));
  Serial.print(temperatureC, 4);
  Serial.print(F(" degC / "));
  Serial.print(DallasTemperature::toFahrenheit(temperatureC), 4);
  Serial.println(F(" degF"));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("Arduino Uno DS18B20 diagnostic"));
  Serial.print(F("DATA pin: D"));
  Serial.println(ONE_WIRE_PIN);
  scanBus();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleAt = now;

  if (!activeSensor) {
    scanBus();
    return;
  }
  printTemperature();
}
