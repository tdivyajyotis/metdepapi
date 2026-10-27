#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoJson.h>
#include <CertStoreBearSSL.h>
#include <DallasTemperature.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <WiFiClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <Wire.h>
#include <coredecls.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

extern "C" {
#include <lwip/apps/sntp.h>
}

#include <Adafruit_SHT4x.h>
#include <Adafruit_TCS3448.h>

#include "Tsl2584.h"
#include "OfflineQueue.h"
#include "config.h"
#include "gts_root_r4.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define DEVICE_API_TOKEN ""
#define ALLOW_INSECURE_TLS 0
#warning "Copy include/secrets.example.h to include/secrets.h before deployment"
#endif

namespace {

OneWire oneWire(config::ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
SoftwareSerial gpsSerial(config::GPS_RX_PIN, config::GPS_TX_PIN);
TinyGPSPlus gps;
Adafruit_ADS1115 ads1115;
Adafruit_SHT4x sht45;
Adafruit_TCS3448 tcs3448;
Tsl2584 tslSea(config::TSL2584_SEA_ADDRESS);
Tsl2584 tslLand(config::TSL2584_LAND_ADDRESS);
BearSSL::CertStore certificateStore;
offline::Queue offlineQueue;

bool hasSht45 = false;
bool hasTcs3448 = false;
bool hasTslSea = false;
bool hasTslLand = false;
bool hasAds1115 = false;
bool certificateStoreReady = false;
int certificateStoreCount = 0;

struct TelemetryEvent {
  uint32_t id = 0;
  uint32_t uptimeMs = 0;
  char source[12] = {};
  char level[8] = {};
  char message[config::TELEMETRY_MESSAGE_LENGTH] = {};
};

TelemetryEvent telemetryEvents[config::TELEMETRY_MAX_EVENTS];
uint8_t telemetryEventCount = 0;
uint32_t nextTelemetryEventId = 1;
uint32_t droppedTelemetryEvents = 0;
uint8_t i2cAddresses[16] = {};
uint8_t i2cAddressCount = 0;
uint32_t wifiConnectAttempts = 0;
uint32_t wifiConnectEvents = 0;
bool wifiAttemptActive = false;
bool missingWifiConfigReported = false;
uint32_t wifiAttemptStartedAt = 0;
uint32_t lastWifiProgressAt = 0;
wl_status_t previousWifiStatus = WL_IDLE_STATUS;
uint32_t adsReads = 0;
uint32_t adsFailures = 0;
uint32_t gpsUartOverflows = 0;
uint32_t lastGpsByteAt = 0;
uint32_t lastSensorRecoveryAt = 0;
uint32_t adsLastSuccessAt = 0;
uint32_t sht45LastSuccessAt = 0;
uint32_t tslSeaLastSuccessAt = 0;
uint32_t tslLandLastSuccessAt = 0;
uint32_t tcs3448LastSuccessAt = 0;
uint32_t ds18b20LastSuccessAt[config::DS18B20_COUNT] = {};
uint32_t adsReadFailures = 0;
uint32_t sht45ReadFailures = 0;
uint32_t tslSeaReadFailures = 0;
uint32_t tslLandReadFailures = 0;
uint32_t tcs3448ReadFailures = 0;
uint32_t ds18b20ReadFailures[config::DS18B20_COUNT] = {};
uint32_t postAttempts = 0;
uint32_t postSuccesses = 0;
uint32_t postFailures = 0;
int postLastStatus = 0;

enum class TimeSource : uint8_t {
  // Keep the public numeric values stable. Value 1 belonged to removed
  // hardware and remains intentionally unused.
  Unsynchronized = 0,
  Ntp = 2,
  Gps = 3,
  GpsHoldover = 4,
};

TimeSource timeSource = TimeSource::Unsynchronized;
volatile bool ntpSyncArrived = false;
bool ntpSyncReceived = false;
bool ntpClockApplied = false;
bool ntpStartupWaitReported = false;
uint32_t lastGpsTimeAt = 0;
uint32_t lastGpsDisciplineAt = 0;

uint32_t lastSampleAt = 0;
uint32_t lastWifiAttemptAt = 0;
uint32_t lastReplayAttemptAt = 0;
uint32_t sequenceNumber = 0;
uint32_t bootId = 0;

void addTelemetryEvent(const char *source, const char *level,
                       const char *format, ...) {
  char message[config::TELEMETRY_MESSAGE_LENGTH] = {};
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);

  if (telemetryEventCount == config::TELEMETRY_MAX_EVENTS) {
    memmove(&telemetryEvents[0], &telemetryEvents[1],
            sizeof(TelemetryEvent) * (config::TELEMETRY_MAX_EVENTS - 1));
    --telemetryEventCount;
    ++droppedTelemetryEvents;
  }

  TelemetryEvent &event = telemetryEvents[telemetryEventCount++];
  event.id = nextTelemetryEventId++;
  event.uptimeMs = millis();
  snprintf(event.source, sizeof(event.source), "%s", source);
  snprintf(event.level, sizeof(event.level), "%s", level);
  snprintf(event.message, sizeof(event.message), "%s", message);
  Serial.printf("[%s] %s: %s\n", source, level, message);
}

const char *timeSourceName(TimeSource source) {
  switch (source) {
    case TimeSource::Ntp:
      return "ntp";
    case TimeSource::Gps:
      return "gps";
    case TimeSource::GpsHoldover:
      return "gps_holdover";
    default:
      return "unsynchronized";
  }
}

void changeTimeSource(TimeSource nextSource, const char *reason) {
  if (timeSource == nextSource) {
    return;
  }
  const TimeSource previousSource = timeSource;
  timeSource = nextSource;
  addTelemetryEvent("time", "info", "Source %s -> %s (%s)",
                    timeSourceName(previousSource), timeSourceName(nextSource),
                    reason);
}

bool systemTimeIsValid() {
  return time(nullptr) >= config::MIN_VALID_UNIX_TIME;
}

bool clockReadyForData() {
  return ntpClockApplied && systemTimeIsValid();
}

void setSystemTime(time_t epoch) {
  timeval value = {epoch, 0};
  settimeofday(&value, nullptr);
}

void initializeTimekeeping() {
  settimeofday_cb([](bool fromSntp) {
    if (fromSntp) {
      ntpSyncArrived = true;
    }
  });
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
}

void pumpGps() {
  while (gpsSerial.available() > 0) {
    gps.encode(static_cast<char>(gpsSerial.read()));
    lastGpsByteAt = millis();
  }
  if (gpsSerial.overflow()) {
    ++gpsUartOverflows;
  }
}

bool gpsReceiverIsFresh() {
  return lastGpsByteAt != 0 &&
         millis() - lastGpsByteAt <= config::GPS_PRESENT_MAX_AGE_MS;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
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

time_t gpsEpoch() {
  if (!gpsCalendarFieldsValid()) return 0;
  const int64_t days = daysFromCivil(gps.date.year(), gps.date.month(),
                                     gps.date.day());
  const int64_t seconds = days * 86400LL + gps.time.hour() * 3600LL +
                          gps.time.minute() * 60LL + gps.time.second();
  return seconds >= config::MIN_VALID_UNIX_TIME
             ? static_cast<time_t>(seconds)
             : 0;
}

bool gpsFixIsFresh() {
  return gps.location.isValid() &&
         gps.location.age() <= config::GPS_MAX_FIX_AGE_MS &&
         gps.satellites.isValid() &&
         gps.satellites.age() <= config::GPS_MAX_FIX_AGE_MS &&
         gps.satellites.value() >= config::GPS_MIN_SATELLITES;
}

bool gpsTimeIsFresh() {
  return gpsCalendarFieldsValid() &&
         gps.date.age() <= config::GPS_MAX_TIME_AGE_MS &&
         gps.time.age() <= config::GPS_MAX_TIME_AGE_MS &&
         gpsEpoch() >= config::MIN_VALID_UNIX_TIME;
}

void pollTimeSources() {
  const uint32_t now = millis();
  const bool validGpsTime = gpsTimeIsFresh();
  if (validGpsTime) {
    lastGpsTimeAt = now;
  }

  // NTP is the mandatory startup authority. The SNTP callback runs only after
  // the ESP8266 system clock has been set successfully. GPS is deliberately
  // prevented from stopping SNTP or opening the sampling gate before this.
  if (ntpSyncArrived) {
    ntpSyncArrived = false;
    ntpSyncReceived = true;
    if (systemTimeIsValid()) {
      changeTimeSource(TimeSource::Ntp, "initial NTP synchronization");
      ntpClockApplied = true;
      addTelemetryEvent("time", "info",
                        "Initial system clock synchronized from NTP");
      sntp_stop();
    }
  }

  // NMEA time validity is independent of navigation-fix validity. A receiver
  // may emit checksum-valid UTC in RMC status V or GGA fix quality 0. Use that
  // time for the system clock while continuing to suppress invalid position.
  if (ntpClockApplied && validGpsTime) {
    const bool firstGpsSync = timeSource != TimeSource::Gps;
    const bool disciplineDue =
        lastGpsDisciplineAt == 0 ||
        now - lastGpsDisciplineAt >= config::GPS_DISCIPLINE_INTERVAL_MS;
    if (firstGpsSync || disciplineDue) {
      const time_t epoch = gpsEpoch();
      if (epoch >= config::MIN_VALID_UNIX_TIME) {
        sntp_stop();
        setSystemTime(epoch);
        lastGpsDisciplineAt = now;
        addTelemetryEvent("time", "info", "System clock disciplined from GPS");
      }
    }
    changeTimeSource(TimeSource::Gps,
                     "fresh GPS time; position fix not required");
  } else if (timeSource == TimeSource::Gps &&
             now - lastGpsTimeAt > config::GPS_TIME_HOLDOVER_DELAY_MS) {
    changeTimeSource(TimeSource::GpsHoldover, "GPS time became stale");
  }

}

String romToString(const DeviceAddress address) {
  char text[17] = {};
  for (uint8_t i = 0; i < 8; ++i) {
    snprintf(text + i * 2, sizeof(text) - i * 2, "%02X", address[i]);
  }
  return String(text);
}

String formatIso8601(time_t epoch, int32_t offsetSeconds,
                     const char *suffix) {
  if (epoch < config::MIN_VALID_UNIX_TIME) {
    return String();
  }
  const time_t adjusted = epoch + offsetSeconds;
  struct tm utc {};
  gmtime_r(&adjusted, &utc);
  char timestamp[32] = {};
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &utc);
  strncat(timestamp, suffix, sizeof(timestamp) - strlen(timestamp) - 1);
  return String(timestamp);
}

String iso8601Utc(time_t epoch) {
  return formatIso8601(epoch, 0, "Z");
}

String iso8601Ist(time_t epoch) {
  return formatIso8601(epoch, config::IST_OFFSET_SECONDS, "+05:30");
}

const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_found";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_WRONG_PASSWORD:
      return "wrong_password";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "unknown";
  }
}

void connectWifi() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    wifiAttemptActive = false;
    return;
  }

  if (strlen(WIFI_SSID) == 0) {
    if (!missingWifiConfigReported) {
      missingWifiConfigReported = true;
      addTelemetryEvent("wifi", "error", "WIFI_SSID is empty");
    }
    return;
  }

  const uint32_t now = millis();
  if (wifiAttemptActive) {
    if (now - wifiAttemptStartedAt < config::WIFI_CONNECT_TIMEOUT_MS) {
      return;
    }

    wifiAttemptActive = false;
    lastWifiAttemptAt = now;
    addTelemetryEvent("wifi", "warn", "Attempt timed out after %lu ms; status=%s",
                      static_cast<unsigned long>(now - wifiAttemptStartedAt),
                      wifiStatusName(status));
    WiFi.disconnect(false);
    return;
  }

  if (lastWifiAttemptAt != 0 &&
      now - lastWifiAttemptAt < config::WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastWifiAttemptAt = now;
  wifiAttemptStartedAt = now;
  lastWifiProgressAt = now;
  wifiAttemptActive = true;
  ++wifiConnectAttempts;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(config::DEVICE_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  addTelemetryEvent("wifi", "info", "Starting attempt %lu to SSID '%s'",
                    static_cast<unsigned long>(wifiConnectAttempts), WIFI_SSID);
}

void monitorWifiStatus() {
  const wl_status_t status = WiFi.status();
  if (status != previousWifiStatus) {
    previousWifiStatus = status;
    if (status == WL_CONNECTED) {
      ++wifiConnectEvents;
      addTelemetryEvent("wifi", "info", "Connected; IP=%s RSSI=%d dBm",
                        WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      addTelemetryEvent("wifi", "warn", "Status=%s (%d)",
                        wifiStatusName(status), static_cast<int>(status));
    }
  }

  const uint32_t now = millis();
  if (wifiAttemptActive && status != WL_CONNECTED &&
      now - lastWifiProgressAt >= config::WIFI_STATUS_LOG_INTERVAL_MS) {
    lastWifiProgressAt = now;
    Serial.printf("WiFi waiting: status=%s (%d), attempt=%lu, elapsed=%lu ms\n",
                  wifiStatusName(status), static_cast<int>(status),
                  static_cast<unsigned long>(wifiConnectAttempts),
                  static_cast<unsigned long>(now - wifiAttemptStartedAt));
  }
}

bool soilPercent(int16_t counts, uint8_t channel, float &percent) {
  const int16_t dry = config::SOIL_DRY_COUNTS[channel];
  const int16_t wet = config::SOIL_WET_COUNTS[channel];
  if (dry == wet) {
    return false;
  }
  percent = 100.0f * static_cast<float>(counts - dry) /
            static_cast<float>(wet - dry);
  percent = constrain(percent, 0.0f, 100.0f);
  return true;
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void logSensorRecovery(const char *name, bool available) {
  if (available) {
    addTelemetryEvent("sensors", "info", "%s recovered", name);
  }
}

bool initializeAds1115() {
  if (!i2cDevicePresent(config::ADS1115_ADDRESS) ||
      !ads1115.begin(config::ADS1115_ADDRESS, &Wire)) {
    return false;
  }
  ads1115.setGain(GAIN_TWOTHIRDS);
  ads1115.setDataRate(RATE_ADS1115_128SPS);
  return true;
}

bool initializeSht45() {
  if (!i2cDevicePresent(config::SHT45_ADDRESS) || !sht45.begin(&Wire)) {
    return false;
  }
  sht45.setPrecision(SHT4X_HIGH_PRECISION);
  sht45.setHeater(SHT4X_NO_HEATER);
  return true;
}

bool initializeTcs3448() {
  return i2cDevicePresent(config::TCS3448_ADDRESS) && tcs3448.begin(&Wire) &&
         tcs3448.setGain(TCS3448_GAIN_64X) && tcs3448.setATIME(29) &&
         tcs3448.setASTEP(599) &&
         tcs3448.setSMUXMode(TCS3448_SMUX_18CH);
}

void recoverUnavailableSensors(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - lastSensorRecoveryAt <
                    config::SENSOR_RECOVERY_INTERVAL_MS) {
    return;
  }
  lastSensorRecoveryAt = now;

  if (!hasAds1115) {
    hasAds1115 = initializeAds1115();
    if (!force) logSensorRecovery("ADS1115", hasAds1115);
  }
  if (!hasSht45) {
    hasSht45 = initializeSht45();
    if (!force) logSensorRecovery("SHT45", hasSht45);
  }
  if (!hasTslSea) {
    hasTslSea = tslSea.begin(Wire);
    if (!force) logSensorRecovery("TSL sea", hasTslSea);
  }
  if (!hasTslLand) {
    hasTslLand = tslLand.begin(Wire);
    if (!force) logSensorRecovery("TSL land", hasTslLand);
  }
  if (!hasTcs3448) {
    hasTcs3448 = initializeTcs3448();
    if (!force) logSensorRecovery("TCS3448", hasTcs3448);
  }
}

int16_t readAdsMedian(uint8_t channel) {
  int16_t samples[config::ADS1115_MEDIAN_SAMPLES] = {};
  for (uint8_t i = 0; i < config::ADS1115_MEDIAN_SAMPLES; ++i) {
    samples[i] = ads1115.readADC_SingleEnded(channel);
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
  return samples[config::ADS1115_MEDIAN_SAMPLES / 2];
}

bool captureAds1115(offline::ReadingRecord &record) {
  ++adsReads;
  if (!hasAds1115 || !i2cDevicePresent(config::ADS1115_ADDRESS)) {
    hasAds1115 = false;
    ++adsFailures;
    ++adsReadFailures;
    return false;
  }
  record.flags |= offline::ADS1115_PRESENT;
  const int16_t values[2] = {readAdsMedian(0), readAdsMedian(1)};
  for (uint8_t channel = 0; channel < 2; ++channel) {
    if (values[channel] < config::ADS1115_MIN_COUNTS ||
        values[channel] > config::ADS1115_MAX_COUNTS) {
      ++adsFailures;
      ++adsReadFailures;
      addTelemetryEvent("ads1115", "warn",
                        "A%u out of 0-5 V range: %d counts", channel,
                        values[channel]);
      return false;
    }
    record.soilCounts[channel] = values[channel];
    record.soilMillivolts[channel] = static_cast<uint16_t>(
        lroundf(values[channel] * config::ADS1115_LSB_MV));
  }
  record.flags |= offline::ADS1115_OK;
  adsLastSuccessAt = millis();
  return true;
}

bool recordFlag(const offline::ReadingRecord &record, offline::ReadingFlag flag) {
  return (record.flags & static_cast<uint32_t>(flag)) != 0;
}

int16_t centiSigned(float value) {
  return static_cast<int16_t>(constrain(lroundf(value * 100.0f), -32768L,
                                        32767L));
}

uint16_t centiUnsigned(float value) {
  return static_cast<uint16_t>(constrain(lroundf(value * 100.0f), 0L,
                                         65535L));
}

void captureDs18b20(offline::ReadingRecord &record) {
  ds18b20.requestTemperatures();
  const uint32_t conversionStartedAt = millis();
  while (millis() - conversionStartedAt < config::DS18B20_CONVERSION_MS) {
    pumpGps();
    delay(1);
  }
  for (uint8_t i = 0; i < config::DS18B20_COUNT; ++i) {
    DeviceAddress address = {};
    memcpy(address, config::DS18B20_ROMS[i], sizeof(DeviceAddress));
    const bool present = OneWire::crc8(address, 7) == address[7] &&
                         ds18b20.isConnected(address);
    if (present) {
      record.flags |= static_cast<uint32_t>(offline::DS1_PRESENT) << i;
    }
    if (!present) {
      ++ds18b20ReadFailures[i];
      continue;
    }
    const float temperature = ds18b20.getTempC(address);
    if (temperature != DEVICE_DISCONNECTED_C && temperature >= -55.0f &&
        temperature <= 125.0f) {
      record.flags |= static_cast<uint32_t>(offline::DS1_OK) << i;
      record.dsTemperatureCenti[i] = centiSigned(temperature);
      ds18b20LastSuccessAt[i] = millis();
    } else {
      ++ds18b20ReadFailures[i];
    }
  }
}

void captureSht45(offline::ReadingRecord &record) {
  if (!hasSht45) {
    return;
  }
  sensors_event_t humidity;
  sensors_event_t temperature;
  if (!sht45.getEvent(&humidity, &temperature)) {
    ++sht45ReadFailures;
    hasSht45 = false;
    return;
  }
  sht45LastSuccessAt = millis();
  record.flags |= offline::SHT45_OK;
  record.shtTemperatureCenti = centiSigned(temperature.temperature);
  record.shtHumidityCenti = centiUnsigned(humidity.relative_humidity);
}

void captureTsl(offline::ReadingRecord &record, Tsl2584 &sensor,
                bool &available, bool sea) {
  Tsl2584Reading reading;
  if (!available || !sensor.read(reading)) {
    if (available) {
      if (sea) {
        ++tslSeaReadFailures;
      } else {
        ++tslLandReadFailures;
      }
      available = false;
    }
    return;
  }
  if (sea) {
    tslSeaLastSuccessAt = millis();
  } else {
    tslLandLastSuccessAt = millis();
  }
  record.flags |= sea ? offline::TSL_SEA_OK : offline::TSL_LAND_OK;
  if (reading.saturated) {
    record.flags |= sea ? offline::TSL_SEA_SATURATED
                        : offline::TSL_LAND_SATURATED;
  }
  // Keep the packed member expression visible to the compiler. Record format
  // 4 also aligns this complete 16-bit region, preventing ESP8266 exception 9
  // when template/library code binds a field by reference.
  if (sea) {
    record.tslSea[0] = reading.broadbandCounts;
    record.tslSea[1] = reading.infraredCounts;
    record.tslSea[2] = reading.visibleCounts;
  } else {
    record.tslLand[0] = reading.broadbandCounts;
    record.tslLand[1] = reading.infraredCounts;
    record.tslLand[2] = reading.visibleCounts;
  }
}

void captureTcs3448(offline::ReadingRecord &record) {
  if (!hasTcs3448) {
    return;
  }
  uint16_t readings[TCS3448_CHANNEL_COUNT] = {};
  if (!tcs3448.readAllChannels(readings)) {
    ++tcs3448ReadFailures;
    hasTcs3448 = false;
    return;
  }
  tcs3448LastSuccessAt = millis();
  record.flags |= offline::TCS3448_OK;
  const uint8_t channels[13] = {
      TCS3448_CHANNEL_F1,       TCS3448_CHANNEL_F2,
      TCS3448_CHANNEL_FZ,       TCS3448_CHANNEL_F3,
      TCS3448_CHANNEL_F4,       TCS3448_CHANNEL_F5,
      TCS3448_CHANNEL_FY,       TCS3448_CHANNEL_FXL,
      TCS3448_CHANNEL_F6,       TCS3448_CHANNEL_F7,
      TCS3448_CHANNEL_F8,       TCS3448_CHANNEL_NIR,
      TCS3448_CHANNEL_VIS_TL_0};
  for (uint8_t i = 0; i < 13; ++i) {
    record.tcs3448[i] = readings[channels[i]];
  }
}

void addDs18b20Json(JsonObject sensors,
                    const offline::ReadingRecord &record) {
  JsonArray probes = sensors.createNestedArray("ds18b20");
  for (uint8_t i = 0; i < config::DS18B20_COUNT; ++i) {
    DeviceAddress address = {};
    memcpy(address, config::DS18B20_ROMS[i], sizeof(DeviceAddress));
    JsonObject probe = probes.createNestedObject();
    char suffix[3] = {};
    snprintf(suffix, sizeof(suffix), "%02X", address[7]);
    probe["sensor_id"] = i + 1;
    probe["depth_cm"] = config::DS18B20_DEPTH_CM[i];
    probe["rom"] = romToString(address);
    probe["rom_suffix"] = suffix;
    probe["present"] = recordFlag(
        record, static_cast<offline::ReadingFlag>(offline::DS1_PRESENT << i));
    const bool ok = recordFlag(
        record, static_cast<offline::ReadingFlag>(offline::DS1_OK << i));
    probe["ok"] = ok;
    if (ok) {
      probe["temperature_c"] = record.dsTemperatureCenti[i] / 100.0f;
    }
  }
}

void addSht45Json(JsonObject sensors, const offline::ReadingRecord &record) {
  JsonObject out = sensors.createNestedObject("sht45");
  const bool ok = recordFlag(record, offline::SHT45_OK);
  out["ok"] = ok;
  if (ok) {
    out["temperature_c"] = record.shtTemperatureCenti / 100.0f;
    out["humidity_pct"] = record.shtHumidityCenti / 100.0f;
  }
}

void addSoilJson(JsonObject sensors, const offline::ReadingRecord &record) {
  JsonArray probes = sensors.createNestedArray("soil_moisture");
  const bool ok = recordFlag(record, offline::ADS1115_OK);
  for (uint8_t channel = 0; channel < 2; ++channel) {
    JsonObject probe = probes.createNestedObject();
    probe["channel"] = channel;
    probe["sensor_id"] = channel + 1;
    probe["adc_channel"] = channel == 0 ? "A0" : "A1";
    probe["depth_cm"] = config::SOIL_DEPTH_CM[channel];
    probe["sensor_type"] = "resistive_lm393_1p3m";
    probe["adc"] = "ads1115_16bit";
    probe["pga_range_v"] = 6.144f;
    probe["ok"] = ok;
    if (!ok) {
      continue;
    }
    probe["raw_counts"] = record.soilCounts[channel];
    probe["voltage_v"] = record.soilMillivolts[channel] / 1000.0f;
    float percent = 0;
    if (soilPercent(record.soilCounts[channel], channel, percent)) {
      probe["moisture_pct"] = percent;
    } else {
      probe["moisture_pct"] = nullptr;
    }
  }
}

void addTslJson(JsonObject sensors, const char *name, uint8_t address,
                uint16_t broadband, uint16_t infrared, uint16_t visible,
                bool ok, bool saturated) {
  JsonObject out = sensors.createNestedObject(name);
  out["address"] = address;
  out["ok"] = ok;
  if (ok) {
    out["broadband_counts"] = broadband;
    out["infrared_counts"] = infrared;
    out["visible_counts"] = visible;
    out["saturated"] = saturated;
  }
}

void addTcs3448Json(JsonObject sensors,
                    const offline::ReadingRecord &record) {
  JsonObject out = sensors.createNestedObject("tcs3448");
  const bool ok = recordFlag(record, offline::TCS3448_OK);
  out["ok"] = ok;
  if (!ok) {
    return;
  }
  const char *names[13] = {
      "f1_405nm", "f2_425nm", "fz_450nm", "f3_475nm", "f4_515nm",
      "f5_550nm", "fy_555nm", "fxl_600nm", "f6_640nm", "f7_690nm",
      "f8_745nm", "nir_855nm", "visible"};
  for (uint8_t i = 0; i < 13; ++i) {
    out[names[i]] = record.tcs3448[i];
  }
}

void addTimeAndGpsJson(JsonObject sensors,
                       const offline::ReadingRecord &record) {
  JsonObject timing = sensors.createNestedObject("timekeeping");
  const bool validSystemTime = recordFlag(record, offline::SYSTEM_TIME_VALID);
  timing["ok"] = validSystemTime;
  timing["source"] =
      timeSourceName(static_cast<TimeSource>(record.timeSource));
  timing["timezone"] = "Asia/Kolkata";
  timing["utc_offset"] = "+05:30";
  if (validSystemTime) {
    timing["system_epoch"] = record.observedEpoch;
    timing["utc"] = iso8601Utc(record.observedEpoch);
    timing["ist"] = iso8601Ist(record.observedEpoch);
  }
  JsonObject out = sensors.createNestedObject("gps");
  const bool validFix = recordFlag(record, offline::GPS_FIX_VALID);
  const bool validGpsTime = recordFlag(record, offline::GPS_TIME_VALID);
  out["fix_valid"] = validFix;
  out["time_valid"] = validGpsTime;
  out["time_requires_fix"] = false;
  out["present"] = recordFlag(record, offline::GPS_PRESENT);
  out["connection"] = "direct_uart_d6";
  out["satellites"] = record.gpsSatellites;
  if (validGpsTime) {
    out["time_utc"] = iso8601Utc(record.gpsEpoch);
    out["time_ist"] = iso8601Ist(record.gpsEpoch);
  }
  if (recordFlag(record, offline::GPS_HDOP_VALID)) {
    out["hdop"] = record.gpsHdopCenti / 100.0f;
  }
  if (validFix) {
    out["latitude_deg"] = record.gpsLatitudeE7 / 10000000.0;
    out["longitude_deg"] = record.gpsLongitudeE7 / 10000000.0;
    out["fix_age_ms"] = record.gpsFixAgeMs;
    if (recordFlag(record, offline::GPS_ALTITUDE_VALID)) {
      out["altitude_m"] = record.gpsAltitudeMm / 1000.0f;
    }
  }
}

uint8_t configuredDs18b20PresentCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < config::DS18B20_COUNT; ++i) {
    DeviceAddress address = {};
    memcpy(address, config::DS18B20_ROMS[i], sizeof(DeviceAddress));
    if (OneWire::crc8(address, 7) == address[7] &&
        ds18b20.isConnected(address)) {
      ++count;
    }
  }
  return count;
}

offline::ReadingRecord captureReading() {
  offline::ReadingRecord record;
  record.bootId = bootId;
  record.sequence = sequenceNumber;
  record.uptimeMs = millis();
  snprintf(record.firmware, sizeof(record.firmware), "%s",
           config::FIRMWARE_VERSION);
  if (WiFi.status() == WL_CONNECTED) {
    record.wifiRssiDbm = static_cast<int8_t>(WiFi.RSSI());
  }

  captureAds1115(record);
  pollTimeSources();

  const time_t systemEpoch = time(nullptr);
  if (systemEpoch >= config::MIN_VALID_UNIX_TIME) {
    record.flags |= offline::SYSTEM_TIME_VALID;
    record.observedEpoch = static_cast<uint32_t>(systemEpoch);
  }
  record.timeSource = static_cast<uint8_t>(timeSource);

  captureDs18b20(record);
  captureSht45(record);
  captureTsl(record, tslSea, hasTslSea, true);
  captureTsl(record, tslLand, hasTslLand, false);
  captureTcs3448(record);

  if (gpsReceiverIsFresh()) {
    record.flags |= offline::GPS_PRESENT;
  }
  record.gpsSatellites =
      gps.satellites.isValid() &&
              gps.satellites.age() <= config::GPS_MAX_FIX_AGE_MS
                             ? static_cast<uint8_t>(std::min<uint32_t>(
                                   gps.satellites.value(), 255))
                             : 0;
  if (gpsTimeIsFresh()) {
    record.flags |= offline::GPS_TIME_VALID;
    record.gpsEpoch = static_cast<uint32_t>(gpsEpoch());
  }
  if (gps.hdop.isValid() &&
      gps.hdop.age() <= config::GPS_MAX_FIX_AGE_MS) {
    record.flags |= offline::GPS_HDOP_VALID;
    record.gpsHdopCenti =
        static_cast<uint16_t>(std::min<uint32_t>(gps.hdop.value(), 65535));
  }
  if (gpsFixIsFresh()) {
    record.flags |= offline::GPS_FIX_VALID;
    record.gpsFixAgeMs = gps.location.age();
    record.gpsLatitudeE7 =
        static_cast<int32_t>(lround(gps.location.lat() * 1e7));
    record.gpsLongitudeE7 =
        static_cast<int32_t>(lround(gps.location.lng() * 1e7));
    if (gps.altitude.isValid() &&
        gps.altitude.age() <= config::GPS_MAX_FIX_AGE_MS) {
      record.flags |= offline::GPS_ALTITUDE_VALID;
      record.gpsAltitudeMm =
          static_cast<int32_t>(lround(gps.altitude.meters() * 1000.0));
    }
  }
  record.gpsCharsProcessed = gps.charsProcessed();
  record.gpsSentencesOk = gps.passedChecksum();
  record.gpsChecksumFailures = gps.failedChecksum();
  record.adsReads = adsReads;
  record.adsFailures = adsFailures;
  return record;
}

void addHealthEntry(JsonObject parent, const char *name, bool detected,
                    bool readingOk, uint32_t lastSuccessAt,
                    uint32_t readFailures) {
  JsonObject health = parent.createNestedObject(name);
  health["detected"] = detected;
  health["reading_ok"] = readingOk;
  health["read_failures"] = readFailures;
  if (lastSuccessAt != 0) {
    health["last_success_age_ms"] = millis() - lastSuccessAt;
  }
}

void addTelemetrySnapshot(JsonObject telemetry,
                          const offline::ReadingRecord &record) {
  telemetry["version"] = 2;
  telemetry["captured_uptime_ms"] = millis();
  telemetry["dropped_events"] = droppedTelemetryEvents;

  JsonObject node = telemetry.createNestedObject("nodemcu");
  node["firmware"] = config::FIRMWARE_VERSION;
  node["boot_id"] = bootId;
  node["chip_id"] = ESP.getChipId();
  node["reset_reason"] = ESP.getResetReason();
  node["free_heap_bytes"] = ESP.getFreeHeap();
  node["max_free_heap_block_bytes"] = ESP.getMaxFreeBlockSize();
  node["heap_fragmentation_pct"] = ESP.getHeapFragmentation();
  node["flash_real_size_bytes"] = ESP.getFlashChipRealSize();
  node["cpu_mhz"] = ESP.getCpuFreqMHz();

  JsonObject wifi = telemetry.createNestedObject("wifi");
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  wifi["connected"] = wifiConnected;
  wifi["status"] = static_cast<int>(WiFi.status());
  wifi["status_name"] = wifiStatusName(WiFi.status());
  wifi["connect_attempts"] = wifiConnectAttempts;
  wifi["connect_events"] = wifiConnectEvents;
  wifi["attempt_active"] = wifiAttemptActive;
  if (wifiAttemptActive) {
    wifi["attempt_elapsed_ms"] = millis() - wifiAttemptStartedAt;
  }
  if (wifiConnected) {
    wifi["ip"] = WiFi.localIP().toString();
    wifi["gateway"] = WiFi.gatewayIP().toString();
    wifi["rssi_dbm"] = WiFi.RSSI();
    wifi["channel"] = WiFi.channel();
  }

  JsonObject tls = telemetry.createNestedObject("tls");
  tls["cert_store_ready"] = certificateStoreReady;
  tls["trust_anchor_count"] = certificateStoreCount;
  tls["verification_required"] = ALLOW_INSECURE_TLS == 0;

  JsonObject bus = telemetry.createNestedObject("i2c");
  JsonArray addresses = bus.createNestedArray("addresses");
  for (uint8_t i = 0; i < i2cAddressCount; ++i) {
    addresses.add(i2cAddresses[i]);
  }
  bus["device_count"] = i2cAddressCount;
  bus["sda"] = "D2/GPIO4";
  bus["scl"] = "D1/GPIO5";
  bus["clock_hz"] = config::I2C_CLOCK_HZ;

  JsonObject presence = telemetry.createNestedObject("sensor_presence");
  presence["ds18b20_discovered"] = ds18b20.getDeviceCount();
  presence["ds18b20_configured"] = config::DS18B20_COUNT;
  presence["ds18b20_mapped_present"] = configuredDs18b20PresentCount();
  presence["sht45"] = hasSht45;
  presence["tsl2584_sea"] = hasTslSea;
  presence["tsl2584_land"] = hasTslLand;
  presence["tcs3448"] = hasTcs3448;
  presence["ads1115"] = hasAds1115;
  presence["gps"] = gpsReceiverIsFresh();

  JsonObject sensorHealth = telemetry.createNestedObject("sensor_health");
  JsonObject dsHealth = sensorHealth.createNestedObject("ds18b20");
  JsonArray dsProbes = dsHealth.createNestedArray("probes");
  uint8_t dsOkCount = 0;
  uint32_t dsFailureCount = 0;
  for (uint8_t i = 0; i < config::DS18B20_COUNT; ++i) {
    const bool present = recordFlag(
        record, static_cast<offline::ReadingFlag>(offline::DS1_PRESENT << i));
    const bool ok = recordFlag(
        record, static_cast<offline::ReadingFlag>(offline::DS1_OK << i));
    if (ok) ++dsOkCount;
    dsFailureCount += ds18b20ReadFailures[i];
    JsonObject probe = dsProbes.createNestedObject();
    probe["sensor_id"] = i + 1;
    probe["detected"] = present;
    probe["reading_ok"] = ok;
    probe["read_failures"] = ds18b20ReadFailures[i];
    if (ds18b20LastSuccessAt[i] != 0) {
      probe["last_success_age_ms"] = millis() - ds18b20LastSuccessAt[i];
    }
  }
  dsHealth["reading_ok"] = dsOkCount == config::DS18B20_COUNT;
  dsHealth["ok_count"] = dsOkCount;
  dsHealth["configured_count"] = config::DS18B20_COUNT;
  dsHealth["read_failures"] = dsFailureCount;
  addHealthEntry(sensorHealth, "ads1115", hasAds1115,
                 recordFlag(record, offline::ADS1115_OK), adsLastSuccessAt,
                 adsReadFailures);
  addHealthEntry(sensorHealth, "sht45", hasSht45,
                 recordFlag(record, offline::SHT45_OK), sht45LastSuccessAt,
                 sht45ReadFailures);
  addHealthEntry(sensorHealth, "tsl2584_sea", hasTslSea,
                 recordFlag(record, offline::TSL_SEA_OK), tslSeaLastSuccessAt,
                 tslSeaReadFailures);
  addHealthEntry(sensorHealth, "tsl2584_land", hasTslLand,
                 recordFlag(record, offline::TSL_LAND_OK),
                 tslLandLastSuccessAt, tslLandReadFailures);
  addHealthEntry(sensorHealth, "tcs3448", hasTcs3448,
                 recordFlag(record, offline::TCS3448_OK), tcs3448LastSuccessAt,
                 tcs3448ReadFailures);

  JsonObject adc = telemetry.createNestedObject("ads1115");
  adc["address"] = config::ADS1115_ADDRESS;
  adc["available"] = hasAds1115;
  adc["reading_ok"] = recordFlag(record, offline::ADS1115_OK);
  adc["reads"] = adsReads;
  adc["failures"] = adsFailures;
  adc["pga_range_v"] = 6.144f;

  JsonObject gpsLink = telemetry.createNestedObject("gps_uart");
  gpsLink["pin"] = "D6/GPIO12";
  gpsLink["baud"] = config::GPS_SERIAL_BAUD;
  gpsLink["chars_processed"] = gps.charsProcessed();
  gpsLink["sentences_ok"] = gps.passedChecksum();
  gpsLink["checksum_failures"] = gps.failedChecksum();
  gpsLink["buffer_overflows"] = gpsUartOverflows;
  gpsLink["receiving_recently"] = gpsReceiverIsFresh();
  if (lastGpsByteAt != 0) {
    gpsLink["last_rx_age_ms"] = millis() - lastGpsByteAt;
  }

  JsonObject http = telemetry.createNestedObject("http");
  http["attempts"] = postAttempts;
  http["successes"] = postSuccesses;
  http["failures"] = postFailures;
  http["last_status"] = postLastStatus;

  JsonObject storage = telemetry.createNestedObject("offline_queue");
  storage["ready"] = offlineQueue.ready();
  storage["record_format"] = offline::RECORD_FORMAT_VERSION;
  storage["record_bytes"] = offline::RECORD_BYTES;
  storage["queued_readings"] = offlineQueue.count();
  storage["capacity_readings"] = offlineQueue.capacity();
  storage["file_bytes"] = offlineQueue.fileBytes();
  storage["replayed_this_boot"] = offlineQueue.replayed();
  storage["dropped_this_boot"] = offlineQueue.dropped();
  storage["corrupt_records_at_boot"] = offlineQueue.corruptRecords();

  JsonObject clock = telemetry.createNestedObject("timekeeping");
  const time_t systemEpoch = time(nullptr);
  const bool validSystemTime = systemEpoch >= config::MIN_VALID_UNIX_TIME;
  clock["system_time_valid"] = validSystemTime;
  clock["source"] = timeSourceName(timeSource);
  clock["timezone"] = "Asia/Kolkata";
  clock["utc_offset"] = "+05:30";
  clock["ntp_sync_received"] = ntpSyncReceived;
  clock["ntp_clock_applied"] = ntpClockApplied;
  clock["initial_ntp_required"] = true;
  clock["sampling_enabled"] = clockReadyForData();
  clock["gps_fix_fresh"] = gpsFixIsFresh();
  clock["gps_time_fresh"] = gpsTimeIsFresh();
  clock["gps_calendar_fields_valid"] = gpsCalendarFieldsValid();
  if (gps.date.isValid()) {
    clock["gps_reported_year"] = gps.date.year();
    clock["gps_reported_month"] = gps.date.month();
    clock["gps_reported_day"] = gps.date.day();
  }
  clock["gps_time_used_without_fix"] =
      gpsTimeIsFresh() && !gpsFixIsFresh();
  if (validSystemTime) {
    clock["system_epoch"] = static_cast<uint32_t>(systemEpoch);
    clock["system_utc"] = iso8601Utc(systemEpoch);
    clock["system_ist"] = iso8601Ist(systemEpoch);
  }
  if (gpsTimeIsFresh()) {
    clock["gps_epoch"] = static_cast<uint32_t>(gpsEpoch());
    clock["gps_ist"] = iso8601Ist(gpsEpoch());
  }

  JsonArray events = telemetry.createNestedArray("events");
  for (uint8_t i = 0; i < telemetryEventCount; ++i) {
    const TelemetryEvent &event = telemetryEvents[i];
    JsonObject item = events.createNestedObject();
    item["id"] = event.id;
    item["uptime_ms"] = event.uptimeMs;
    item["source"] = event.source;
    item["level"] = event.level;
    item["message"] = event.message;
  }
}

String makePayload(const offline::ReadingRecord &record, bool fullTelemetry,
                   bool storedOffline, uint16_t queueDepthAtSend) {
  DynamicJsonDocument doc(fullTelemetry ? 9216 : 6144);
  doc["schema_version"] = 1;
  doc["device_id"] = config::DEVICE_ID;
  doc["firmware"] = record.firmware;
  doc["sequence"] = record.sequence;

  char eventId[64] = {};
  snprintf(eventId, sizeof(eventId), "%s-%08lx-%lu", config::DEVICE_ID,
           static_cast<unsigned long>(record.bootId),
           static_cast<unsigned long>(record.sequence));
  doc["event_id"] = eventId;

  // Store an offset-aware IST timestamp. PostgreSQL timestamptz normalizes it
  // to the same UTC instant, while humans and raw payload logs see local time.
  const String observedAt = iso8601Ist(record.observedEpoch);
  if (observedAt.length() > 0) {
    doc["observed_at"] = observedAt;
  } else {
    doc["observed_at"] = nullptr;
  }
  doc["uptime_ms"] = record.uptimeMs;
  doc["wifi_rssi_dbm"] = record.wifiRssiDbm;

  JsonObject sensors = doc.createNestedObject("sensors");
  addDs18b20Json(sensors, record);
  addSht45Json(sensors, record);
  addSoilJson(sensors, record);
  addTslJson(sensors, "tsl2584_sea", config::TSL2584_SEA_ADDRESS,
             record.tslSea[0], record.tslSea[1], record.tslSea[2],
             recordFlag(record, offline::TSL_SEA_OK),
             recordFlag(record, offline::TSL_SEA_SATURATED));
  addTslJson(sensors, "tsl2584_land", config::TSL2584_LAND_ADDRESS,
             record.tslLand[0], record.tslLand[1], record.tslLand[2],
             recordFlag(record, offline::TSL_LAND_OK),
             recordFlag(record, offline::TSL_LAND_SATURATED));
  addTcs3448Json(sensors, record);
  addTimeAndGpsJson(sensors, record);

  JsonObject telemetry = doc.createNestedObject("telemetry");
  if (fullTelemetry) {
    addTelemetrySnapshot(telemetry, record);
  } else {
    telemetry["version"] = 2;
    JsonObject node = telemetry.createNestedObject("nodemcu");
    node["firmware"] = record.firmware;
    node["boot_id"] = record.bootId;
    JsonObject adc = telemetry.createNestedObject("ads1115");
    adc["available"] = recordFlag(record, offline::ADS1115_PRESENT);
    adc["reading_ok"] = recordFlag(record, offline::ADS1115_OK);
    adc["reads"] = record.adsReads;
    adc["failures"] = record.adsFailures;
  }
  JsonObject delivery = telemetry.createNestedObject("delivery");
  delivery["stored_offline"] = storedOffline;
  delivery["record_format"] = offline::RECORD_FORMAT_VERSION;
  delivery["queue_depth_at_send"] = queueDepthAtSend;
  if (storedOffline && record.observedEpoch >= config::MIN_VALID_UNIX_TIME &&
      systemTimeIsValid()) {
    const time_t now = time(nullptr);
    delivery["queue_delay_seconds"] =
        now > record.observedEpoch ? static_cast<uint32_t>(now - record.observedEpoch)
                                   : 0;
  }

  if (doc.overflowed()) {
    addTelemetryEvent("json", "error", "Reading payload exceeded JSON capacity");
    return String();
  }

  String payload;
  const size_t payloadLength = measureJson(doc);
  if (!payload.reserve(payloadLength + 1)) {
    addTelemetryEvent("json", "error", "Could not reserve %u-byte payload",
                      static_cast<unsigned>(payloadLength));
    return String();
  }
  serializeJson(doc, payload);
  return payload;
}

template <typename TClient>
bool postWithClient(TClient &client, const String &payload, const char *url) {
  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    ++postAttempts;
    ++postFailures;
    postLastStatus = -1;
    addTelemetryEvent("http", "error", "HTTP begin failed");
    return false;
  }

  http.addHeader(F("Content-Type"), F("application/json"));
  http.addHeader(F("Authorization"), String(F("Bearer ")) + DEVICE_API_TOKEN);
  http.addHeader(F("X-Device-ID"), config::DEVICE_ID);

  ++postAttempts;
  const int status = http.POST(reinterpret_cast<const uint8_t *>(payload.c_str()),
                               payload.length());
  postLastStatus = status;
  const bool accepted = status >= 200 && status < 300;
  if (accepted) {
    ++postSuccesses;
    addTelemetryEvent("http", "info", "POST returned %d (accepted)", status);
  } else {
    ++postFailures;
    addTelemetryEvent("http", "error", "POST returned %d (not accepted)",
                      status);
  }
  if (!accepted && status > 0) {
    Serial.println(http.getString());
  }
  http.end();
  return accepted;
}

bool postPayload(const String &payload, const char *url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Not posting: WiFi disconnected"));
    return false;
  }
  if (!clockReadyForData()) {
    Serial.println(F("Not posting: initial NTP clock gate is closed"));
    return false;
  }

  const String destination(url);
  if (destination.startsWith("https://")) {
    if (!systemTimeIsValid()) {
      Serial.println(F("Not posting HTTPS: waiting for initial NTP sync"));
      return false;
    }
    BearSSL::WiFiClientSecure client;
    client.setBufferSizes(config::TLS_RECEIVE_BUFFER_BYTES,
                          config::TLS_TRANSMIT_BUFFER_BYTES);
    Serial.printf(
        "TLS start: payload=%u free_heap=%u max_block=%u buffers=%u/%u\n",
        static_cast<unsigned>(payload.length()), ESP.getFreeHeap(),
        ESP.getMaxFreeBlockSize(),
        static_cast<unsigned>(config::TLS_RECEIVE_BUFFER_BYTES),
        static_cast<unsigned>(config::TLS_TRANSMIT_BUFFER_BYTES));
    if (certificateStoreReady) {
      client.setCertStore(&certificateStore);
      return postWithClient(client, payload, url);
    }
#ifdef TLS_ROOT_CA_PEM_OVERRIDE
    const char *rootCa = TLS_ROOT_CA_PEM_OVERRIDE;
#else
    const char *rootCa = GTS_ROOT_R4_PEM;
#endif
    if (strlen(rootCa) > 0) {
      BearSSL::X509List trustAnchor(rootCa);
      client.setTrustAnchors(&trustAnchor);
      return postWithClient(client, payload, url);
    }
#if ALLOW_INSECURE_TLS
    Serial.println(F("WARNING: HTTPS certificate verification is disabled"));
    client.setInsecure();
    return postWithClient(client, payload, url);
#else
    Serial.println(F("Not posting: no TLS root CA is configured"));
    return false;
#endif
  }

  WiFiClient client;
  return postWithClient(client, payload, url);
}

bool readyToSend() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  return clockReadyForData();
}

String makeBatchPayload(const offline::ReadingRecord *records, uint8_t count,
                        uint16_t queueDepthAtSend) {
  String batch;
  if (!batch.reserve(7000)) {
    addTelemetryEvent("json", "error", "Could not reserve batch payload");
    return String();
  }
  batch = F("{\"readings\":[");
  for (uint8_t i = 0; i < count; ++i) {
    const String reading =
        makePayload(records[i], false, true, queueDepthAtSend);
    if (reading.length() == 0) {
      return String();
    }
    if (i > 0) {
      batch += ',';
    }
    batch += reading;
  }
  batch += F("]}");
  return batch;
}

uint32_t timeUntilNextSample(uint32_t now) {
  if (lastSampleAt == 0) {
    return 0;
  }
  const uint32_t elapsed = now - lastSampleAt;
  return elapsed >= config::SAMPLE_INTERVAL_MS
             ? 0
             : config::SAMPLE_INTERVAL_MS - elapsed;
}

void drainOfflineQueue() {
  if (!offlineQueue.ready() || offlineQueue.count() == 0 || !readyToSend()) {
    return;
  }
  const uint32_t now = millis();
  if (lastReplayAttemptAt != 0 &&
      now - lastReplayAttemptAt < config::OFFLINE_REPLAY_RETRY_MS) {
    return;
  }
  if (timeUntilNextSample(now) <= config::OFFLINE_REPLAY_SAMPLE_GUARD_MS) {
    return;
  }
  lastReplayAttemptAt = now;

  const uint8_t count = min(
      config::OFFLINE_REPLAY_BATCH_SIZE,
      static_cast<uint8_t>(min(offlineQueue.count(), static_cast<uint16_t>(255))));
  offline::ReadingRecord records[config::OFFLINE_REPLAY_BATCH_SIZE];
  for (uint8_t i = 0; i < count; ++i) {
    if (!offlineQueue.peek(records[i], i)) {
      addTelemetryEvent("queue", "error", "Could not read queued record %u", i);
      return;
    }
  }
  const uint16_t depthAtSend = offlineQueue.count();
  const String payload = makeBatchPayload(records, count, depthAtSend);
  if (payload.length() == 0) {
    return;
  }
  Serial.printf("Replaying %u queued reading(s); %u queued before send\n", count,
                depthAtSend);
  if (!postPayload(payload, config::BATCH_INGEST_URL)) {
    return;
  }
  if (!offlineQueue.acknowledge(count)) {
    addTelemetryEvent("queue", "error",
                      "Batch accepted but local acknowledgement failed");
    return;
  }
  addTelemetryEvent("queue", "info", "Replayed %u; %u reading(s) remain",
                    count, offlineQueue.count());
}

void initializeTlsTrustStore() {
  if (!LittleFS.begin()) {
    addTelemetryEvent("tls", "warn",
                      "LittleFS mount failed; using compiled fallback");
    return;
  }

  if (offlineQueue.begin(LittleFS)) {
    addTelemetryEvent("queue", "info",
                      "Offline queue ready: %u/%u reading(s), %u bytes",
                      offlineQueue.count(), offlineQueue.capacity(),
                      static_cast<unsigned>(offlineQueue.fileBytes()));
    if (offlineQueue.corruptRecords() > 0) {
      addTelemetryEvent("queue", "warn", "%lu corrupt record(s) ignored",
                        static_cast<unsigned long>(
                            offlineQueue.corruptRecords()));
    }
  } else {
    addTelemetryEvent("queue", "error", "Offline queue initialization failed");
  }

  certificateStoreCount = certificateStore.initCertStore(
      LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  if (certificateStoreCount <= 0) {
    addTelemetryEvent("tls", "warn",
                      "No CA-store certificates; using compiled fallback");
    return;
  }

  certificateStoreReady = true;
  addTelemetryEvent("tls", "info", "Loaded %d trust anchors from LittleFS",
                    certificateStoreCount);
}

void scanI2cBus() {
  Serial.println(F("I2C scan D1=SCL D2=SDA:"));
  i2cAddressCount = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X\n", address);
      if (i2cAddressCount < sizeof(i2cAddresses)) {
        i2cAddresses[i2cAddressCount++] = address;
      }
    }
    yield();
  }
  addTelemetryEvent("i2c", "info", "Scan found %u device(s) on D1/D2",
                    i2cAddressCount);
}

void initializeSensors() {
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(false);
  recoverUnavailableSensors(true);
}

void printSensorStatus() {
  addTelemetryEvent(
      "sensors", "info",
      "DS18B20=%u/%u ADS1115=%s GPS=D6 SHT45=%s TSL-Sea=%s TSL-Land=%s TCS=%s",
      configuredDs18b20PresentCount(), config::DS18B20_COUNT,
      hasAds1115 ? "ok" : "missing", hasSht45 ? "ok" : "missing",
      hasTslSea ? "ok" : "missing",
      hasTslLand ? "ok" : "missing", hasTcs3448 ? "ok" : "missing");
  for (uint8_t i = 0; i < config::DS18B20_COUNT; ++i) {
    DeviceAddress address = {};
    memcpy(address, config::DS18B20_ROMS[i], sizeof(DeviceAddress));
    addTelemetryEvent("ds18b20", ds18b20.isConnected(address) ? "info" : "warn",
                      "Sensor %u ROM=%s depth=%ucm %s", i + 1,
                      romToString(address).c_str(), config::DS18B20_DEPTH_CM[i],
                      ds18b20.isConnected(address) ? "present" : "missing");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("NodeMCU sensor node starting"));

  bootId = ESP.getChipId() ^ micros() ^ ESP.getCycleCount();
  addTelemetryEvent("boot", "info", "NodeMCU %s starting; reset=%s",
                    config::FIRMWARE_VERSION, ESP.getResetReason().c_str());
  initializeTlsTrustStore();
  gpsSerial.begin(config::GPS_SERIAL_BAUD, SWSERIAL_8N1,
                  config::GPS_RX_PIN, config::GPS_TX_PIN, false,
                  config::GPS_RX_BUFFER_BYTES,
                  config::GPS_ISR_BUFFER_WORDS);
  Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
  Wire.setClock(config::I2C_CLOCK_HZ);
  scanI2cBus();
  initializeSensors();
  initializeTimekeeping();
  printSensorStatus();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectWifi();
}

void loop() {
  pumpGps();
  connectWifi();
  monitorWifiStatus();
  pollTimeSources();
  recoverUnavailableSensors();

  const uint32_t now = millis();
  const bool samplingReady = clockReadyForData();
  if (!samplingReady && !ntpStartupWaitReported) {
    ntpStartupWaitReported = true;
    addTelemetryEvent("time", "info",
                      "Waiting for initial NTP sync before first sample");
  }
  if (samplingReady &&
      (lastSampleAt == 0 ||
       now - lastSampleAt >= config::SAMPLE_INTERVAL_MS)) {
    lastSampleAt = now;
    ++sequenceNumber;
    offline::ReadingRecord record = captureReading();

    bool delivered = false;
    if (offlineQueue.count() == 0 && readyToSend()) {
      const String payload = makePayload(record, true, false, 0);
      if (payload.length() > 0) {
        Serial.println(payload);
        delivered = postPayload(payload, config::INGEST_URL);
      }
    }
    if (!delivered) {
      if (offlineQueue.enqueue(record)) {
        addTelemetryEvent("queue", "info",
                          "Stored sequence %lu locally; %u reading(s) queued",
                          static_cast<unsigned long>(record.sequence),
                          offlineQueue.count());
      } else {
        addTelemetryEvent("queue", "error",
                          "Could not store sequence %lu locally",
                          static_cast<unsigned long>(record.sequence));
      }
    }
  }

  drainOfflineQueue();

  pumpGps();
  delay(2);
}
