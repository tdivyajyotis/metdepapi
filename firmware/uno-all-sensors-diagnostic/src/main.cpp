#include <Adafruit_SHT4x.h>
#include <Adafruit_TCS3448.h>
#include <AltSoftSerial.h>
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <RTClib.h>
#include <TinyGPSPlus.h>
#include <Wire.h>

#include "Tsl2584.h"
#include "config.h"

namespace {

RTC_DS3231 rtc;
Adafruit_SHT4x sht45;
Adafruit_TCS3448 tcs3448;
Tsl2584 tslSea(config::TSL2584_SEA_ADDRESS);
Tsl2584 tslLand(config::TSL2584_LAND_ADDRESS);
OneWire oneWire(config::ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
AltSoftSerial gpsSerial;
TinyGPSPlus gps;

bool runRequested = true;

void pumpGps() {
  if (!config::ENABLE_GPS) {
    return;
  }
  while (gpsSerial.available() > 0) {
    gps.encode(static_cast<char>(gpsSerial.read()));
  }
}

void pumpGpsFor(uint32_t durationMs) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < durationMs) {
    pumpGps();
  }
}

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printAddress(uint8_t address) {
  Serial.print(F("0x"));
  printHexByte(address);
}

void printRom(const DeviceAddress address) {
  for (uint8_t index = 0; index < 8; ++index) {
    printHexByte(address[index]);
  }
}

const __FlashStringHelper *i2cName(uint8_t address) {
  switch (address) {
    case config::TSL2584_SEA_ADDRESS:
      return F("TSL2584 sea");
    case config::SHT45_ADDRESS:
      return F("SHT45");
    case config::TSL2584_LAND_ADDRESS:
      return F("TSL2584 land");
    case config::DS3231_EEPROM_ADDRESS:
      return F("DS3231 module EEPROM");
    case config::TCS3448_ADDRESS:
      return F("TCS3448");
    case config::DS3231_ADDRESS:
      return F("DS3231 RTC");
    default:
      return F("unknown");
  }
}

uint8_t scanI2c() {
  Serial.println(F("\n[I2C scan]"));
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("  FOUND "));
      printAddress(address);
      Serial.print(F("  "));
      Serial.println(i2cName(address));
      ++found;
    } else if (error == 4) {
      Serial.print(F("  BUS ERROR at "));
      printAddress(address);
      Serial.println();
    }
  }
  Serial.print(F("  Devices responding: "));
  Serial.println(found);
  return found;
}

bool testRtc() {
  Serial.println(F("\n[DS3231 RTC @ 0x68]"));
  if (!rtc.begin(&Wire)) {
    Serial.println(F("  FAIL: begin() could not find the RTC"));
    return false;
  }
  const DateTime now = rtc.now();
  const bool plausible = now.year() >= 2021 && now.year() <= 2099;
  Serial.print(F("  lostPower="));
  Serial.println(rtc.lostPower() ? F("true") : F("false"));
  Serial.print(F("  time="));
  Serial.print(now.year());
  Serial.print('-');
  if (now.month() < 10) Serial.print('0');
  Serial.print(now.month());
  Serial.print('-');
  if (now.day() < 10) Serial.print('0');
  Serial.print(now.day());
  Serial.print(' ');
  if (now.hour() < 10) Serial.print('0');
  Serial.print(now.hour());
  Serial.print(':');
  if (now.minute() < 10) Serial.print('0');
  Serial.print(now.minute());
  Serial.print(':');
  if (now.second() < 10) Serial.print('0');
  Serial.println(now.second());
  Serial.println(plausible ? F("  PASS: RTC read succeeded")
                           : F("  FAIL: RTC returned an implausible year"));
  return plausible;
}

bool testSht45() {
  Serial.println(F("\n[SHT45 @ 0x44]"));
  if (!sht45.begin(&Wire)) {
    Serial.println(F("  FAIL: begin() could not identify the sensor"));
    return false;
  }
  sht45.setPrecision(SHT4X_HIGH_PRECISION);
  sht45.setHeater(SHT4X_NO_HEATER);
  sensors_event_t humidity;
  sensors_event_t temperature;
  if (!sht45.getEvent(&humidity, &temperature)) {
    Serial.println(F("  FAIL: getEvent() failed"));
    return false;
  }
  Serial.print(F("  temperature_c="));
  Serial.println(temperature.temperature, 4);
  Serial.print(F("  humidity_pct="));
  Serial.println(humidity.relative_humidity, 4);
  const bool plausible = temperature.temperature >= -40.0f &&
                         temperature.temperature <= 125.0f &&
                         humidity.relative_humidity >= 0.0f &&
                         humidity.relative_humidity <= 100.0f;
  Serial.println(plausible ? F("  PASS: reading is plausible")
                           : F("  FAIL: reading is outside sensor limits"));
  return plausible;
}

bool testTsl(const __FlashStringHelper *name, Tsl2584 &sensor) {
  Serial.println();
  Serial.print('[');
  Serial.print(name);
  Serial.print(F(" @ "));
  printAddress(sensor.address());
  Serial.println(']');
  if (!sensor.begin(Wire)) {
    Serial.println(F("  FAIL: ID/configuration sequence failed"));
    return false;
  }
  delay(120);  // Production integration is nominally 100 ms.
  Tsl2584Reading reading;
  if (!sensor.read(reading)) {
    Serial.println(F("  FAIL: ADC-valid bit or data read failed"));
    return false;
  }
  Serial.print(F("  broadband_counts="));
  Serial.println(reading.broadbandCounts);
  Serial.print(F("  infrared_counts="));
  Serial.println(reading.infraredCounts);
  Serial.print(F("  visible_counts="));
  Serial.println(reading.visibleCounts);
  Serial.print(F("  saturated="));
  Serial.println(reading.saturated ? F("true") : F("false"));
  Serial.println(F("  PASS: configured and read successfully"));
  return true;
}

void printTcsChannel(const __FlashStringHelper *name, uint16_t value) {
  Serial.print(F("  "));
  Serial.print(name);
  Serial.print('=');
  Serial.println(value);
}

bool testTcs3448() {
  Serial.println(F("\n[TCS3448 @ 0x59]"));
  if (!tcs3448.begin()) {
    Serial.println(F("  FAIL: begin() could not identify the sensor"));
    return false;
  }
  const bool configured = tcs3448.setGain(TCS3448_GAIN_64X) &&
                          tcs3448.setATIME(29) &&
                          tcs3448.setASTEP(599) &&
                          tcs3448.setSMUXMode(TCS3448_SMUX_18CH);
  if (!configured) {
    Serial.println(F("  FAIL: production gain/integration/SMUX setup failed"));
    return false;
  }
  uint16_t values[TCS3448_CHANNEL_COUNT] = {};
  if (!tcs3448.readAllChannels(values)) {
    Serial.println(F("  FAIL: readAllChannels() failed"));
    return false;
  }
  printTcsChannel(F("f1_405nm"), values[TCS3448_CHANNEL_F1]);
  printTcsChannel(F("f2_425nm"), values[TCS3448_CHANNEL_F2]);
  printTcsChannel(F("fz_450nm"), values[TCS3448_CHANNEL_FZ]);
  printTcsChannel(F("f3_475nm"), values[TCS3448_CHANNEL_F3]);
  printTcsChannel(F("f4_515nm"), values[TCS3448_CHANNEL_F4]);
  printTcsChannel(F("f5_550nm"), values[TCS3448_CHANNEL_F5]);
  printTcsChannel(F("fy_555nm"), values[TCS3448_CHANNEL_FY]);
  printTcsChannel(F("fxl_600nm"), values[TCS3448_CHANNEL_FXL]);
  printTcsChannel(F("f6_640nm"), values[TCS3448_CHANNEL_F6]);
  printTcsChannel(F("f7_690nm"), values[TCS3448_CHANNEL_F7]);
  printTcsChannel(F("f8_745nm"), values[TCS3448_CHANNEL_F8]);
  printTcsChannel(F("nir_855nm"), values[TCS3448_CHANNEL_NIR]);
  printTcsChannel(F("visible"), values[TCS3448_CHANNEL_VIS_TL_0]);
  Serial.println(F("  PASS: configured and read all channels"));
  return true;
}

int8_t mappedDsIndex(const DeviceAddress address) {
  for (uint8_t index = 0; index < config::DS18B20_COUNT; ++index) {
    if (memcmp(address, config::DS18B20_ROMS[index], sizeof(DeviceAddress)) == 0) {
      return index;
    }
  }
  return -1;
}

bool testDs18b20() {
  Serial.println(F("\n[DS18B20 1-Wire bus]"));
  if (!config::ENABLE_DS18B20) {
    Serial.println(F("  SKIP: disabled in include/config.h"));
    return true;
  }
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(true);
  const uint8_t count = ds18b20.getDeviceCount();
  Serial.print(F("  data_pin=D"));
  Serial.println(config::ONE_WIRE_PIN);
  Serial.print(F("  devices_found="));
  Serial.println(count);
  Serial.print(F("  power_mode="));
  Serial.println(ds18b20.isParasitePowerMode() ? F("parasite")
                                               : F("external"));
  if (count == 0) {
    Serial.println(F("  FAIL: no devices; check DATA and the 4.7 kOhm pull-up"));
    return false;
  }

  ds18b20.requestTemperatures();
  uint8_t validReadings = 0;
  for (uint8_t device = 0; device < count; ++device) {
    DeviceAddress address = {};
    if (!ds18b20.getAddress(address, device)) {
      Serial.print(F("  FAIL: address read failed for index "));
      Serial.println(device);
      continue;
    }
    const bool crcValid = OneWire::crc8(address, 7) == address[7];
    const float temperature = ds18b20.getTempC(address);
    const bool temperatureValid = temperature != DEVICE_DISCONNECTED_C &&
                                  temperature >= -55.0f && temperature <= 125.0f;
    const int8_t mapped = mappedDsIndex(address);
    Serial.print(F("  ROM="));
    printRom(address);
    Serial.print(F(" crc="));
    Serial.print(crcValid ? F("valid") : F("INVALID"));
    Serial.print(F(" temperature_c="));
    Serial.print(temperature, 4);
    if (mapped >= 0) {
      Serial.print(F(" sensor_id="));
      Serial.print(mapped + 1);
      Serial.print(F(" depth_cm="));
      Serial.print(config::DS18B20_DEPTH_CM[mapped]);
    } else {
      Serial.print(F(" mapping=unassigned"));
    }
    Serial.println();
    if (crcValid && temperatureValid) {
      ++validReadings;
    }
  }
  Serial.print(F("  "));
  if (validReadings == count) {
    Serial.println(F("PASS: every discovered probe returned a valid reading"));
    return true;
  }
  Serial.println(F("FAIL: one or more probes returned invalid data"));
  return false;
}

uint16_t readMedian(uint8_t pin) {
  uint16_t samples[config::ADC_SAMPLE_COUNT] = {};
  for (uint8_t index = 0; index < config::ADC_SAMPLE_COUNT; ++index) {
    samples[index] = analogRead(pin);
    delay(5);
  }
  for (uint8_t i = 1; i < config::ADC_SAMPLE_COUNT; ++i) {
    const uint16_t value = samples[i];
    uint8_t j = i;
    while (j > 0 && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = value;
  }
  return samples[config::ADC_SAMPLE_COUNT / 2];
}

bool testAnalogInputs() {
  Serial.println(F("\n[Soil analog inputs]"));
  if (!config::ENABLE_ANALOG_INPUTS) {
    Serial.println(F("  SKIP: disabled in include/config.h"));
    return true;
  }
  for (uint8_t channel = 0; channel < 2; ++channel) {
    const uint16_t counts = readMedian(config::SOIL_ANALOG_PINS[channel]);
    const float volts = counts * config::ADC_REFERENCE_V / 1023.0f;
    Serial.print(F("  A"));
    Serial.print(channel);
    Serial.print(F(" depth_cm="));
    Serial.print(config::SOIL_DEPTH_CM[channel]);
    Serial.print(F(" median_counts="));
    Serial.print(counts);
    Serial.print(F(" voltage_v="));
    Serial.println(volts, 4);
  }
  Serial.println(F("  PASS: ADC conversions completed (calibration not assessed)"));
  return true;
}

void printGpsUtc() {
  if (gps.date.year() < 1000) {
    Serial.print(F("invalid"));
    return;
  }
  Serial.print(gps.date.year());
  Serial.print('-');
  if (gps.date.month() < 10) Serial.print('0');
  Serial.print(gps.date.month());
  Serial.print('-');
  if (gps.date.day() < 10) Serial.print('0');
  Serial.print(gps.date.day());
  Serial.print('T');
  if (gps.time.hour() < 10) Serial.print('0');
  Serial.print(gps.time.hour());
  Serial.print(':');
  if (gps.time.minute() < 10) Serial.print('0');
  Serial.print(gps.time.minute());
  Serial.print(':');
  if (gps.time.second() < 10) Serial.print('0');
  Serial.print(gps.time.second());
  Serial.print(F("Z"));
}

bool testGps() {
  Serial.println(F("\n[GPS NMEA via AltSoftSerial]"));
  if (!config::ENABLE_GPS) {
    Serial.println(F("  SKIP: disabled in include/config.h"));
    return true;
  }

  // Long sensor operations can temporarily fill the software-UART buffer.
  // Use a fresh uninterrupted window to determine whether the receiver is
  // actually transmitting valid NMEA rather than judging stale parser state.
  pumpGps();
  const uint32_t charsBefore = gps.charsProcessed();
  const uint32_t passedBefore = gps.passedChecksum();
  const uint32_t failedBefore = gps.failedChecksum();
  pumpGpsFor(config::GPS_TEST_WINDOW_MS);
  const uint32_t charsNow = gps.charsProcessed();
  const uint32_t passedNow = gps.passedChecksum();
  const uint32_t failedNow = gps.failedChecksum();
  const uint32_t newChars = charsNow - charsBefore;
  const uint32_t newPassed = passedNow - passedBefore;
  const uint32_t newFailed = failedNow - failedBefore;

  Serial.print(F("  wiring=GPS_TX->D"));
  Serial.print(config::GPS_RX_PIN);
  Serial.print(F(" baud="));
  Serial.println(config::GPS_SERIAL_BAUD);
  Serial.print(F("  test_window_ms="));
  Serial.println(config::GPS_TEST_WINDOW_MS);
  Serial.print(F("  new_chars="));
  Serial.println(newChars);
  Serial.print(F("  new_sentences_checksum_ok="));
  Serial.println(newPassed);
  Serial.print(F("  new_sentences_checksum_failed="));
  Serial.println(newFailed);
  Serial.print(F("  total_chars="));
  Serial.println(charsNow);
  Serial.print(F("  total_sentences_checksum_ok="));
  Serial.println(passedNow);
  Serial.print(F("  total_sentences_with_fix="));
  Serial.println(gps.sentencesWithFix());

  // Match production semantics: checksum-valid UTC and navigation position
  // are independent. A receiver may provide useful UTC while reporting no fix.
  const bool fixValid = gps.location.isValid() &&
                        gps.location.age() <= config::GPS_MAX_POSITION_AGE_MS;
  const bool timeValid = gps.date.isValid() && gps.time.isValid() &&
                         gps.date.age() <= config::GPS_MAX_DATE_AGE_MS &&
                         gps.time.age() <= config::GPS_MAX_TIME_AGE_MS;

  Serial.print(F("  fix_valid="));
  Serial.println(fixValid ? F("true") : F("false"));
  Serial.print(F("  time_valid="));
  Serial.println(timeValid ? F("true") : F("false"));
  Serial.print(F("  satellites="));
  Serial.println(gps.satellites.isValid() ? gps.satellites.value() : 0);

  if (gps.date.isValid() && gps.time.isValid()) {
    Serial.print(F("  utc="));
    printGpsUtc();
    Serial.print(F(" date_age_ms="));
    Serial.print(gps.date.age());
    Serial.print(F(" time_age_ms="));
    Serial.println(gps.time.age());
  } else {
    Serial.println(F("  utc=unavailable"));
  }
  if (fixValid) {
    Serial.print(F("  latitude_deg="));
    Serial.println(gps.location.lat(), 6);
    Serial.print(F("  longitude_deg="));
    Serial.println(gps.location.lng(), 6);
    Serial.print(F("  fix_age_ms="));
    Serial.println(gps.location.age());
    if (gps.altitude.isValid()) {
      Serial.print(F("  altitude_m="));
      Serial.println(gps.altitude.meters(), 2);
    }
    if (gps.hdop.isValid()) {
      Serial.print(F("  hdop="));
      Serial.println(gps.hdop.hdop(), 2);
    }
  } else {
    Serial.println(F("  NOTE: no fresh position fix; test outdoors with sky view"));
  }

  const bool receiverHealthy = newChars >= 20 && newPassed > 0;
  if (receiverHealthy) {
    Serial.println(F("  PASS: GPS is transmitting checksum-valid NMEA"));
    if (!fixValid) {
      Serial.println(F("  PASS does not require a satellite position fix"));
    }
  } else if (newChars == 0) {
    Serial.println(F("  FAIL: no serial data; check GPS power, ground, TX and D8"));
  } else {
    Serial.println(F("  FAIL: serial bytes arrived but no valid NMEA checksum"));
  }
  return receiverHealthy;
}

void printHelp() {
  Serial.println(F("Commands (send one letter):"));
  Serial.println(F("  a/r = all tests     i/s = I2C scan"));
  Serial.println(F("  t = RTC             e = SHT45 environment"));
  Serial.println(F("  l = both TSL2584    c = TCS3448 colour"));
  Serial.println(F("  d = DS18B20         o = soil analog inputs"));
  Serial.println(F("  g = GPS             h/? = help"));
}

void runAllTests() {
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.print(F("Station sensor diagnostic at uptime_ms="));
  Serial.println(millis());
  scanI2c();

  uint8_t passed = 0;
  uint8_t tested = 0;
  ++tested;
  if (testRtc()) ++passed;
  ++tested;
  if (testSht45()) ++passed;
  ++tested;
  if (testTsl(F("TSL2584 sea"), tslSea)) ++passed;
  ++tested;
  if (testTsl(F("TSL2584 land"), tslLand)) ++passed;
  ++tested;
  if (testTcs3448()) ++passed;
  ++tested;
  if (testDs18b20()) ++passed;
  ++tested;
  if (testAnalogInputs()) ++passed;
  ++tested;
  if (testGps()) ++passed;

  Serial.println(F("\n[Summary]"));
  Serial.print(F("  categories_passed="));
  Serial.print(passed);
  Serial.print('/');
  Serial.println(tested);
  Serial.println(passed == tested ? F("  OVERALL PASS")
                                  : F("  OVERALL FAIL (see sections above)"));
  printHelp();
}

void handleSerialCommands() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'a' || command == 'A' || command == 'r' ||
        command == 'R') {
      runRequested = true;
    } else if (command == 'i' || command == 'I' || command == 's' ||
               command == 'S') {
      scanI2c();
      printHelp();
    } else if (command == 't' || command == 'T') {
      testRtc();
    } else if (command == 'e' || command == 'E') {
      testSht45();
    } else if (command == 'l' || command == 'L') {
      testTsl(F("TSL2584 sea"), tslSea);
      testTsl(F("TSL2584 land"), tslLand);
    } else if (command == 'c' || command == 'C') {
      testTcs3448();
    } else if (command == 'd' || command == 'D') {
      testDs18b20();
    } else if (command == 'o' || command == 'O') {
      testAnalogInputs();
    } else if (command == 'g' || command == 'G') {
      testGps();
    } else if (command == 'h' || command == 'H' || command == '?') {
      printHelp();
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  delay(1000);
  Wire.begin();
  Wire.setClock(config::I2C_CLOCK_HZ);
  if (config::ENABLE_GPS) {
    gpsSerial.begin(config::GPS_SERIAL_BAUD);
  }

  Serial.println();
  Serial.println(F("Arduino Uno complete station sensor diagnostic"));
  Serial.println(F("I2C: SDA=A4, SCL=A5, 100 kHz"));
  Serial.println(F("GPS: TX->D8 using AltSoftSerial at 9600 baud"));
  Serial.println(F("Uses the production NodeMCU sensor settings and read calls."));
  printHelp();
}

void loop() {
  pumpGps();
  handleSerialCommands();
  if (!runRequested) {
    return;
  }
  runRequested = false;
  runAllTests();
}
