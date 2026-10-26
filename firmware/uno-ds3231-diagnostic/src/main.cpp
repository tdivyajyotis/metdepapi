#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint8_t DS3231_ADDRESS = 0x68;
constexpr uint8_t MODULE_EEPROM_ADDRESS = 0x57;
constexpr uint32_t MIN_VALID_UNIX_TIME = 1609459200UL;  // 2021-01-01 UTC
constexpr uint32_t IST_OFFSET_SECONDS = 5UL * 60UL * 60UL + 30UL * 60UL;
constexpr uint32_t LIVE_REPORT_INTERVAL_MS = 1000;

RTC_DS3231 rtc;
bool rtcAvailable = false;
bool liveOutput = true;
uint32_t lastLiveReportAt = 0;

void printTwoDigits(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

void printIso8601(const DateTime &value, const __FlashStringHelper *suffix) {
  Serial.print(value.year());
  Serial.print('-');
  printTwoDigits(value.month());
  Serial.print('-');
  printTwoDigits(value.day());
  Serial.print('T');
  printTwoDigits(value.hour());
  Serial.print(':');
  printTwoDigits(value.minute());
  Serial.print(':');
  printTwoDigits(value.second());
  Serial.print(suffix);
}

bool dateFieldsArePlausible(const DateTime &value) {
  return value.year() >= 2021 && value.year() <= 2099 &&
         value.month() >= 1 && value.month() <= 12 && value.day() >= 1 &&
         value.day() <= 31 && value.hour() <= 23 && value.minute() <= 59 &&
         value.second() <= 59 && value.unixtime() >= MIN_VALID_UNIX_TIME;
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool readRtcRegisters(uint8_t firstRegister, uint8_t *buffer, uint8_t length) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(firstRegister);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t received = Wire.requestFrom(DS3231_ADDRESS, length);
  if (received != length) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (uint8_t index = 0; index < length; ++index) {
    buffer[index] = Wire.read();
  }
  return true;
}

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

bool printRawRtcRegisters(const __FlashStringHelper *label) {
  // 0x00..0x12: time/calendar, alarms, control, status, aging and temperature.
  uint8_t registers[0x13] = {};
  Serial.print(F("  raw_"));
  Serial.print(label);
  Serial.print('=');
  if (!readRtcRegisters(0x00, registers, sizeof(registers))) {
    Serial.println(F("READ_ERROR"));
    return false;
  }
  for (uint8_t index = 0; index < sizeof(registers); ++index) {
    if (index > 0) {
      Serial.print(' ');
    }
    printHexByte(registers[index]);
  }
  Serial.println();
  Serial.print(F("  control_0x0e=0x"));
  printHexByte(registers[0x0E]);
  Serial.print(F(" eosc="));
  Serial.print((registers[0x0E] & 0x80) ? 1 : 0);
  Serial.print(F(" status_0x0f=0x"));
  printHexByte(registers[0x0F]);
  Serial.print(F(" osf="));
  Serial.print((registers[0x0F] & 0x80) ? 1 : 0);
  Serial.print(F(" busy="));
  Serial.println((registers[0x0F] & 0x04) ? 1 : 0);
  return true;
}

float readRtcTemperatureC() {
  // RTClib 2.1.4 reads the signed DS3231 temperature MSB as uint8_t on AVR.
  // Normalize values above +127 C back into the chip's signed range.
  float temperature = rtc.getTemperature();
  if (temperature >= 128.0f) {
    temperature -= 256.0f;
  }
  return temperature;
}

void scanI2c() {
  Serial.println(F("\n[I2C scan]"));
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      ++found;
      Serial.print(F("  found 0x"));
      if (address < 0x10) {
        Serial.print('0');
      }
      Serial.print(address, HEX);
      if (address == DS3231_ADDRESS) {
        Serial.print(F(" (DS3231 RTC)"));
      } else if (address == MODULE_EEPROM_ADDRESS) {
        Serial.print(F(" (common module EEPROM; not used)"));
      }
      Serial.println();
    } else if (error == 4) {
      Serial.print(F("  bus error at 0x"));
      if (address < 0x10) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }
  Serial.print(F("  devices_found="));
  Serial.println(found);
}

void printClockReading() {
  if (!rtcAvailable) {
    Serial.println(F("RTC unavailable; press r to retry."));
    return;
  }

  const DateTime utc = rtc.now();
  const DateTime ist(utc.unixtime() + IST_OFFSET_SECONDS);
  const bool plausible = dateFieldsArePlausible(utc);

  Serial.print(F("rtc_epoch="));
  Serial.print(utc.unixtime());
  Serial.print(F(" rtc_utc="));
  printIso8601(utc, F("Z"));
  Serial.print(F(" rtc_ist="));
  printIso8601(ist, F("+05:30"));
  Serial.print(F(" valid="));
  Serial.print(plausible ? F("true") : F("false"));
  Serial.print(F(" lost_power="));
  Serial.print(rtc.lostPower() ? F("true") : F("false"));
  Serial.print(F(" temperature_c="));
  Serial.println(readRtcTemperatureC(), 2);
}

void runDiagnostic() {
  Serial.println(F("\n[DS3231 standalone diagnostic]"));
  scanI2c();

  const bool addressPresent = i2cDevicePresent(DS3231_ADDRESS);
  Serial.print(F("  address_0x68="));
  Serial.println(addressPresent ? F("present") : F("MISSING"));

  rtcAvailable = rtc.begin(&Wire);
  if (!rtcAvailable) {
    Serial.println(F("  FAIL: RTClib could not communicate with the DS3231"));
    Serial.println(F("  Check VCC, GND, SDA=A4, SCL=A5 and pull-up voltage."));
    return;
  }

  const bool lostPower = rtc.lostPower();
  const bool firstRawReadOk = printRawRtcRegisters(F("before"));
  const DateTime first = rtc.now();
  const bool plausible = dateFieldsArePlausible(first);
  const float temperature = readRtcTemperatureC();
  const bool temperaturePlausible = temperature >= -40.0f && temperature <= 85.0f;

  Serial.print(F("  lost_power="));
  Serial.println(lostPower ? F("true") : F("false"));
  Serial.print(F("  rtc_epoch="));
  Serial.println(first.unixtime());
  Serial.print(F("  rtc_utc="));
  printIso8601(first, F("Z"));
  Serial.println();
  Serial.print(F("  rtc_ist="));
  printIso8601(DateTime(first.unixtime() + IST_OFFSET_SECONDS), F("+05:30"));
  Serial.println();
  Serial.print(F("  temperature_c="));
  Serial.println(temperature, 2);

  Serial.println(F("  Waiting 2 seconds to verify that the oscillator advances..."));
  delay(2100);
  const bool secondRawReadOk = printRawRtcRegisters(F("after"));
  const DateTime second = rtc.now();
  const int32_t elapsed =
      static_cast<int32_t>(second.unixtime() - first.unixtime());
  Serial.print(F("  rtc_advance_seconds="));
  Serial.println(elapsed);

  bool passed = true;
  if (lostPower) {
    Serial.println(F("  FAIL: oscillator-stop flag is set; install/check the backup battery"));
    Serial.println(F("        and set UTC with: u <unix_epoch>"));
    passed = false;
  }
  if (!plausible) {
    Serial.println(F("  FAIL: RTC date is older than 2021 or otherwise implausible"));
    Serial.println(F("        Set UTC with: u <unix_epoch>"));
    passed = false;
  }
  if (elapsed < 1 || elapsed > 3) {
    Serial.println(F("  FAIL: RTC did not advance at the expected rate"));
    passed = false;
  }
  if (!firstRawReadOk || !secondRawReadOk) {
    Serial.println(F("  FAIL: raw I2C register read was incomplete"));
    passed = false;
  }
  if (!temperaturePlausible) {
    Serial.println(F("  FAIL: temperature register is outside the DS3231 range"));
    passed = false;
  }
  if (passed) {
    Serial.println(F("  PASS: RTC is present, valid, and advancing"));
  }
}

void setRtcFromEpoch(uint32_t epoch) {
  if (!rtcAvailable && !rtc.begin(&Wire)) {
    Serial.println(F("FAIL: RTC unavailable; time not changed"));
    return;
  }
  rtcAvailable = true;
  if (epoch < MIN_VALID_UNIX_TIME) {
    Serial.println(F("FAIL: epoch must be at least 1609459200 (2021-01-01 UTC)"));
    return;
  }

  rtc.adjust(DateTime(epoch));
  const DateTime verify = rtc.now();
  const int32_t error = static_cast<int32_t>(verify.unixtime() - epoch);
  Serial.print(F("RTC written and read back; error_seconds="));
  Serial.println(error);
  printClockReading();
  Serial.println(F("The write clears the DS3231 oscillator-stop/lost-power flag."));
}

void printHelp() {
  Serial.println(F("\nCommands (newline ending):"));
  Serial.println(F("  r            run the complete diagnostic again"));
  Serial.println(F("  p            print one clock reading"));
  Serial.println(F("  l            toggle one-reading-per-second live output"));
  Serial.println(F("  i            scan the I2C bus"));
  Serial.println(F("  u 1791631800 set the RTC from a UTC Unix epoch"));
  Serial.println(F("  h or ?       show this help"));
  Serial.println(F("The RTC always stores UTC; IST is display-only (+05:30)."));
}

void handleCommand(char *line) {
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  if (*line == '\0') {
    return;
  }

  switch (*line) {
    case 'r':
    case 'R':
      runDiagnostic();
      break;
    case 'p':
    case 'P':
      printClockReading();
      break;
    case 'l':
    case 'L':
      liveOutput = !liveOutput;
      Serial.print(F("live_output="));
      Serial.println(liveOutput ? F("on") : F("off"));
      break;
    case 'i':
    case 'I':
      scanI2c();
      break;
    case 'u':
    case 'U': {
      char *argument = line + 1;
      while (*argument == ' ' || *argument == '\t') {
        ++argument;
      }
      char *end = nullptr;
      const unsigned long epoch = strtoul(argument, &end, 10);
      while (end != nullptr && (*end == ' ' || *end == '\t')) {
        ++end;
      }
      if (argument == end || (end != nullptr && *end != '\0')) {
        Serial.println(F("Usage: u <UTC Unix epoch>"));
      } else {
        setRtcFromEpoch(epoch);
      }
      break;
    }
    case 'h':
    case 'H':
    case '?':
      printHelp();
      break;
    default:
      Serial.println(F("Unknown command; press h for help."));
      break;
  }
}

void pollSerial() {
  static char line[32] = {};
  static uint8_t length = 0;
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n' || value == '\r') {
      if (length > 0) {
        line[length] = '\0';
        handleCommand(line);
        length = 0;
      }
    } else if (static_cast<size_t>(length + 1) < sizeof(line)) {
      line[length++] = value;
    } else {
      length = 0;
      Serial.println(F("Command too long; discarded."));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  Wire.begin();  // Uno hardware I2C: SDA=A4, SCL=A5.
  Wire.setClock(I2C_CLOCK_HZ);

  Serial.println();
  Serial.println(F("Arduino Uno / DS3231 RTC diagnostic"));
  Serial.println(F("NodeMCU-compatible policy: UTC storage, >=2021 validity, IST display"));
  Serial.println(F("I2C wiring: SDA=A4, SCL=A5, clock=100 kHz"));
  runDiagnostic();
  printHelp();
  lastLiveReportAt = millis();
}

void loop() {
  pollSerial();
  if (liveOutput && rtcAvailable &&
      millis() - lastLiveReportAt >= LIVE_REPORT_INTERVAL_MS) {
    lastLiveReportAt += LIVE_REPORT_INTERVAL_MS;
    printClockReading();
  }
}

