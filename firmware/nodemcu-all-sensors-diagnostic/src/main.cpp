#include <Adafruit_ADS1X15.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_TCS3448.h>
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <Wire.h>

#include "Tsl2584.h"
#include "config.h"

namespace {

Adafruit_ADS1115 ads1115;
Adafruit_SHT4x sht45;
Adafruit_TCS3448 tcs3448;
Tsl2584 tslSea(config::TSL2584_SEA_ADDRESS);
Tsl2584 tslLand(config::TSL2584_LAND_ADDRESS);
OneWire oneWire(config::ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
SoftwareSerial gpsSerial(config::GPS_RX_PIN, config::GPS_TX_PIN);
TinyGPSPlus gps;

bool fullReportRequested = true;
uint32_t gpsLastByteAt = 0;
uint32_t gpsOverflowCount = 0;

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void printHexByte(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printI2cAddress(uint8_t address) {
  Serial.print(F("0x"));
  printHexByte(address);
}

void printRom(const DeviceAddress address) {
  for (uint8_t index = 0; index < sizeof(DeviceAddress); ++index) {
    printHexByte(address[index]);
  }
}

const __FlashStringHelper *i2cName(uint8_t address) {
  switch (address) {
    case config::TSL2584_SEA_ADDRESS:
      return F("TSL2584 sea");
    case config::SHT45_ADDRESS:
      return F("SHT45");
    case config::ADS1115_ADDRESS:
      return F("ADS1115");
    case config::TSL2584_LAND_ADDRESS:
      return F("TSL2584 land");
    case config::TCS3448_ADDRESS:
      return F("TCS3448");
    default:
      return F("unknown/unexpected");
  }
}

void pumpGps() {
  while (gpsSerial.available() > 0) {
    gps.encode(static_cast<char>(gpsSerial.read()));
    gpsLastByteAt = millis();
  }
  if (gpsSerial.overflow()) ++gpsOverflowCount;
}

void pumpGpsFor(uint32_t durationMs) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < durationMs) {
    pumpGps();
    yield();
  }
}

bool printBoardInfo() {
  Serial.println(F("\n[NodeMCU board]"));
  Serial.print(F("  diagnostic_version="));
  Serial.println(config::DIAGNOSTIC_VERSION);
  Serial.print(F("  chip_id="));
  Serial.println(ESP.getChipId());
  Serial.print(F("  reset_reason="));
  Serial.println(ESP.getResetReason());
  Serial.print(F("  cpu_mhz="));
  Serial.println(ESP.getCpuFreqMHz());
  Serial.print(F("  flash_real_bytes="));
  Serial.println(ESP.getFlashChipRealSize());
  Serial.print(F("  free_heap_bytes="));
  Serial.println(ESP.getFreeHeap());
  Serial.print(F("  heap_fragmentation_pct="));
  Serial.println(ESP.getHeapFragmentation());
  Serial.println(F("  PASS: firmware is running and USB serial is working"));
  return true;
}

uint8_t scanI2cBus() {
  Serial.println();
  Serial.println(F("[I2C scan: D1=SCL, D2=SDA, 100 kHz]"));
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("  FOUND "));
      printI2cAddress(address);
      Serial.print(F("  "));
      Serial.println(i2cName(address));
      ++found;
    } else if (error == 4) {
      Serial.print(F("  BUS ERROR at "));
      printI2cAddress(address);
      Serial.println();
    }
    yield();
  }
  Serial.print(F("  devices_responding="));
  Serial.println(found);
  if (found == 0) {
    Serial.println(F("  FAIL: no response; check this bus's power, ground, SDA and SCL"));
  }
  return found;
}

bool testAds1115() {
  Serial.println(F("\n[ADS1115 @ 0x48: soil A0=15 cm, A1=45 cm]"));
  if (!ads1115.begin(config::ADS1115_ADDRESS, &Wire)) {
    Serial.println(F("  FAIL: begin() could not find ADS1115"));
    return false;
  }
  ads1115.setGain(GAIN_TWOTHIRDS);
  ads1115.setDataRate(RATE_ADS1115_128SPS);
  bool pass = true;
  for (uint8_t channel = 0; channel < 2; ++channel) {
    int16_t samples[config::ADS1115_MEDIAN_SAMPLES] = {};
    for (uint8_t index = 0; index < config::ADS1115_MEDIAN_SAMPLES; ++index) {
      samples[index] = ads1115.readADC_SingleEnded(channel);
      pumpGps();
    }
    for (uint8_t i = 1; i < config::ADS1115_MEDIAN_SAMPLES; ++i) {
      const int16_t value = samples[i];
      int8_t j = static_cast<int8_t>(i) - 1;
      while (j >= 0 && samples[j] > value) {
        samples[j + 1] = samples[j];
        --j;
      }
      samples[j + 1] = value;
    }
    const int16_t counts = samples[config::ADS1115_MEDIAN_SAMPLES / 2];
    const float millivolts = counts * config::ADS1115_LSB_MV;
    const bool inElectricalRange =
        counts >= 0 && counts <= config::ADS1115_MAX_5V_COUNTS;
    Serial.print(F("  A"));
    Serial.print(channel);
    Serial.print(F(" depth_cm="));
    Serial.print(config::SOIL_DEPTH_CM[channel]);
    Serial.print(F(" median_counts="));
    Serial.print(counts);
    Serial.print(F(" voltage_v="));
    Serial.print(millivolts / 1000.0f, 4);
    Serial.print(F(" range="));
    Serial.println(inElectricalRange ? F("ok") : F("INVALID (>5 V or negative)"));
    pass = pass && inElectricalRange;
  }
  Serial.println(pass ? F("  PASS: both ADC channels converted")
                      : F("  FAIL: at least one input is outside 0-5 V"));
  return pass;
}

bool testSht45() {
  Serial.println(F("\n[SHT45 @ 0x44]"));
  if (!i2cDevicePresent(config::SHT45_ADDRESS)) {
    Serial.println(F("  FAIL: address did not acknowledge on D1/D2"));
    return false;
  }
  if (!sht45.begin(&Wire)) {
    Serial.println(F("  FAIL: begin() could not identify SHT45"));
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
  const bool plausible = temperature.temperature >= -40.0f &&
                         temperature.temperature <= 125.0f &&
                         humidity.relative_humidity >= 0.0f &&
                         humidity.relative_humidity <= 100.0f;
  Serial.print(F("  temperature_c="));
  Serial.println(temperature.temperature, 4);
  Serial.print(F("  humidity_pct="));
  Serial.println(humidity.relative_humidity, 4);
  Serial.println(plausible ? F("  PASS: reading is within sensor limits")
                           : F("  FAIL: implausible reading"));
  return plausible;
}

bool testTsl(const __FlashStringHelper *name, Tsl2584 &sensor) {
  Serial.println();
  Serial.print('[');
  Serial.print(name);
  Serial.print(F(" @ "));
  printI2cAddress(sensor.address());
  Serial.println(']');
  if (!i2cDevicePresent(sensor.address())) {
    Serial.println(F("  FAIL: address did not acknowledge on D1/D2"));
    return false;
  }
  if (!sensor.begin(Wire)) {
    Serial.println(F("  FAIL: ID/configuration sequence failed"));
    return false;
  }
  pumpGpsFor(120);
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
  if (!i2cDevicePresent(config::TCS3448_ADDRESS)) {
    Serial.println(F("  FAIL: address did not acknowledge on D1/D2"));
    return false;
  }
  if (!tcs3448.begin(&Wire)) {
    Serial.println(F("  FAIL: begin() could not identify TCS3448"));
    return false;
  }
  if (!tcs3448.setGain(TCS3448_GAIN_64X) || !tcs3448.setATIME(29) ||
      !tcs3448.setASTEP(599) ||
      !tcs3448.setSMUXMode(TCS3448_SMUX_18CH)) {
    Serial.println(F("  FAIL: gain/integration/SMUX setup failed"));
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
  Serial.println(F("\n[DS18B20 1-Wire bus on D5 / GPIO14]"));
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(false);
  const uint8_t count = ds18b20.getDeviceCount();
  Serial.print(F("  devices_found="));
  Serial.println(count);
  Serial.print(F("  expected_mapped_devices="));
  Serial.println(config::DS18B20_COUNT);
  Serial.print(F("  power_mode="));
  Serial.println(ds18b20.isParasitePowerMode() ? F("parasite") : F("external"));
  if (count == 0) {
    Serial.println(F("  FAIL: check DATA and 4.7 kOhm pull-up to 3.3 V"));
    return false;
  }

  ds18b20.requestTemperatures();
  pumpGpsFor(750);  // Match production without starving the GPS UART.
  uint8_t validCount = 0;
  uint8_t mappedCount = 0;
  for (uint8_t device = 0; device < count; ++device) {
    DeviceAddress address = {};
    if (!ds18b20.getAddress(address, device)) continue;
    const bool crcValid = OneWire::crc8(address, 7) == address[7];
    const float temperature = ds18b20.getTempC(address);
    const bool readingValid = temperature != DEVICE_DISCONNECTED_C &&
                              temperature >= -55.0f && temperature <= 125.0f;
    const int8_t mapped = mappedDsIndex(address);
    Serial.print(F("  ROM="));
    printRom(address);
    Serial.print(F(" crc="));
    Serial.print(crcValid ? F("valid") : F("INVALID"));
    Serial.print(F(" temperature_c="));
    Serial.print(temperature, 4);
    if (mapped >= 0) {
      ++mappedCount;
      Serial.print(F(" sensor_id="));
      Serial.print(mapped + 1);
      Serial.print(F(" depth_cm="));
      Serial.print(config::DS18B20_DEPTH_CM[mapped]);
    } else {
      Serial.print(F(" mapping=unassigned"));
    }
    Serial.println();
    if (crcValid && readingValid) ++validCount;
    pumpGps();
  }
  const bool pass = validCount == count && mappedCount == config::DS18B20_COUNT;
  Serial.print(F("  valid_readings="));
  Serial.print(validCount);
  Serial.print('/');
  Serial.println(count);
  Serial.print(F("  known_mappings_present="));
  Serial.print(mappedCount);
  Serial.print('/');
  Serial.println(config::DS18B20_COUNT);
  Serial.println(pass ? F("  PASS: all four mapped probes read correctly")
                      : F("  FAIL: missing, unknown, or invalid probe(s)"));
  return pass;
}

bool leapYear(unsigned year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(unsigned year, unsigned month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && leapYear(year)) return 29;
  return DAYS[month - 1];
}

bool gpsCalendarFieldsValid() {
  if (!gps.date.isValid() || !gps.time.isValid()) return false;
  const unsigned year = gps.date.year();
  const unsigned month = gps.date.month();
  const unsigned day = gps.date.day();
  return year >= 2021 && year <= 2099 && month >= 1 && month <= 12 &&
         day >= 1 && day <= daysInMonth(year, month) &&
         gps.time.hour() < 24 && gps.time.minute() < 60 &&
         gps.time.second() < 60;
}

void printGpsUtc() {
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
  Serial.print('Z');
}

bool testGps() {
  Serial.println(F("\n[GPS: receiver TX -> D6 / GPIO12, 9600 baud]"));
  pumpGps();
  const uint32_t charsBefore = gps.charsProcessed();
  const uint32_t passedBefore = gps.passedChecksum();
  const uint32_t failedBefore = gps.failedChecksum();
  pumpGpsFor(config::GPS_TEST_WINDOW_MS);
  const uint32_t newChars = gps.charsProcessed() - charsBefore;
  const uint32_t newPassed = gps.passedChecksum() - passedBefore;
  const uint32_t newFailed = gps.failedChecksum() - failedBefore;
  const bool receiving = newChars >= 20 && newPassed > 0;
  const bool timeValid = gpsCalendarFieldsValid() &&
                         gps.date.age() <= config::GPS_MAX_FIELD_AGE_MS &&
                         gps.time.age() <= config::GPS_MAX_FIELD_AGE_MS;
  const bool fixValid = gps.location.isValid() &&
                        gps.location.age() <= config::GPS_MAX_FIELD_AGE_MS;
  Serial.print(F("  test_window_ms="));
  Serial.println(config::GPS_TEST_WINDOW_MS);
  Serial.print(F("  new_chars="));
  Serial.println(newChars);
  Serial.print(F("  checksum_ok="));
  Serial.println(newPassed);
  Serial.print(F("  checksum_failed="));
  Serial.println(newFailed);
  Serial.print(F("  uart_overflows="));
  Serial.println(gpsOverflowCount);
  Serial.print(F("  time_valid="));
  Serial.println(timeValid ? F("true") : F("false"));
  Serial.print(F("  calendar_fields_valid="));
  Serial.println(gpsCalendarFieldsValid() ? F("true") : F("false"));
  if (timeValid) {
    Serial.print(F("  utc="));
    printGpsUtc();
    Serial.println();
  } else if (gps.date.isValid() || gps.time.isValid()) {
    Serial.print(F("  raw_gps_fields_rejected="));
    printGpsUtc();
    Serial.println();
  }
  Serial.print(F("  fix_valid="));
  Serial.println(fixValid ? F("true") : F("false"));
  Serial.print(F("  satellites="));
  Serial.println(gps.satellites.isValid() ? gps.satellites.value() : 0);
  if (fixValid) {
    Serial.print(F("  latitude_deg="));
    Serial.println(gps.location.lat(), 6);
    Serial.print(F("  longitude_deg="));
    Serial.println(gps.location.lng(), 6);
    if (gps.altitude.isValid()) {
      Serial.print(F("  altitude_m="));
      Serial.println(gps.altitude.meters(), 2);
    }
    if (gps.hdop.isValid()) {
      Serial.print(F("  hdop="));
      Serial.println(gps.hdop.hdop(), 2);
    }
  } else {
    Serial.println(F("  NOTE: position needs outdoor sky view; UTC may still work"));
  }
  if (receiving) {
    Serial.println(F("  PASS: checksum-valid NMEA received"));
  } else if (newChars == 0) {
    Serial.println(F("  FAIL: no bytes; check GPS power, ground, TX and D6"));
  } else {
    Serial.println(F("  FAIL: bytes arrived but no checksum-valid NMEA"));
  }
  return receiving;
}

void printHelp() {
  Serial.println(F("\nCommands (send one letter):"));
  Serial.println(F("  a/r = all tests       i/s = I2C scan"));
  Serial.println(F("  e = SHT45             l = both TSL2584"));
  Serial.println(F("  c = TCS3448           d = DS18B20"));
  Serial.println(F("  o = ADS1115 soil      g = GPS"));
  Serial.println(F("  b = board status      h/? = help"));
}

void runAllTests() {
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.print(F("NodeMCU station diagnostic; uptime_ms="));
  Serial.println(millis());
  uint8_t passed = 0;
  uint8_t tested = 0;
  ++tested;
  if (printBoardInfo()) ++passed;
  scanI2cBus();
  ++tested;
  if (testAds1115()) ++passed;
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
  while (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());
    if (command == '\r' || command == '\n' || command == ' ') continue;
    switch (command) {
      case 'a':
      case 'A':
      case 'r':
      case 'R':
        fullReportRequested = true;
        break;
      case 'i':
      case 'I':
      case 's':
      case 'S':
        scanI2cBus();
        break;
      case 'e':
      case 'E':
        testSht45();
        break;
      case 'l':
      case 'L':
        testTsl(F("TSL2584 sea"), tslSea);
        testTsl(F("TSL2584 land"), tslLand);
        break;
      case 'c':
      case 'C':
        testTcs3448();
        break;
      case 'd':
      case 'D':
        testDs18b20();
        break;
      case 'o':
      case 'O':
        testAds1115();
        break;
      case 'g':
      case 'G':
        testGps();
        break;
      case 'b':
      case 'B':
        printBoardInfo();
        break;
      case 'h':
      case 'H':
      case '?':
        printHelp();
        break;
      default:
        Serial.print(F("Unknown command: "));
        Serial.println(command);
        printHelp();
        break;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  delay(1000);
  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_CLOCK_HZ);
  gpsSerial.begin(config::GPS_SERIAL_BAUD, SWSERIAL_8N1,
                  config::GPS_RX_PIN, config::GPS_TX_PIN, false,
                  config::GPS_RX_BUFFER_BYTES, config::GPS_ISR_BUFFER_WORDS);

  Serial.println();
  Serial.println(F("NodeMCU complete station diagnostic"));
  Serial.println(F("Serial-only: Wi-Fi/server/LittleFS are not used"));
  Serial.println(F("All I2C devices: D1=SCL/D2=SDA"));
  Serial.println(F("DS18B20=D5; GPS TX->D6"));
  printHelp();
}

void loop() {
  pumpGps();
  handleSerialCommands();
  if (fullReportRequested) {
    fullReportRequested = false;
    runAllTests();
  }
  yield();
}
