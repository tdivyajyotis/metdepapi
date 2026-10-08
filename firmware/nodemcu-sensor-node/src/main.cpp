#include <Arduino.h>
#include <ArduinoJson.h>
#include <CertStoreBearSSL.h>
#include <DallasTemperature.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
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
SoftwareSerial unoSerial(config::UNO_RX_PIN, config::UNO_TX_PIN);
RTC_DS3231 rtc;
Adafruit_SHT4x sht45;
Adafruit_TCS3448 tcs3448;
Tsl2584 tslSea(config::TSL2584_SEA_ADDRESS);
Tsl2584 tslLand(config::TSL2584_LAND_ADDRESS);
BearSSL::CertStore certificateStore;

bool hasSht45 = false;
bool hasTcs3448 = false;
bool hasTslSea = false;
bool hasTslLand = false;
bool hasRtc = false;
bool certificateStoreReady = false;
int certificateStoreCount = 0;

struct TelemetryEvent {
  uint32_t id = 0;
  uint32_t uptimeMs = 0;
  char source[12] = {};
  char level[8] = {};
  char message[config::TELEMETRY_MESSAGE_LENGTH] = {};
};

struct UnoTelemetry {
  bool available = false;
  char firmware[16] = {};
  uint32_t uptimeMs = 0;
  int16_t freeSramBytes = 0;
  uint8_t resetFlags = 0;
  uint32_t commandsReceived = 0;
  uint32_t unknownCommands = 0;
  uint32_t commandOverflows = 0;
  uint32_t adcSamplesCompleted = 0;
  uint32_t lastSampleMs = 0;
  uint32_t loopCount = 0;
  uint32_t gpsCharsProcessed = 0;
  uint32_t gpsSentencesOk = 0;
  uint32_t gpsChecksumFailures = 0;
  bool soilPowerSwitched = false;
};

TelemetryEvent telemetryEvents[config::TELEMETRY_MAX_EVENTS];
uint8_t telemetryEventCount = 0;
uint32_t nextTelemetryEventId = 1;
uint32_t droppedTelemetryEvents = 0;
UnoTelemetry unoTelemetry;
uint8_t i2cAddresses[16] = {};
uint8_t i2cAddressCount = 0;
uint32_t wifiConnectAttempts = 0;
uint32_t wifiConnectEvents = 0;
wl_status_t previousWifiStatus = WL_IDLE_STATUS;
uint32_t unoRequests = 0;
uint32_t unoSuccesses = 0;
uint32_t unoTimeouts = 0;
uint32_t unoInvalidResponses = 0;
uint32_t unoLastLatencyMs = 0;
size_t unoLastResponseBytes = 0;
uint32_t postAttempts = 0;
uint32_t postSuccesses = 0;
uint32_t postFailures = 0;
int postLastStatus = 0;

enum class TimeSource : uint8_t {
  Unsynchronized,
  Rtc,
  Ntp,
  Gps,
  GpsHoldover,
};

TimeSource timeSource = TimeSource::Unsynchronized;
TimeSource rtcLastSetSource = TimeSource::Unsynchronized;
volatile bool ntpSyncArrived = false;
uint32_t lastGpsFixAt = 0;
uint32_t lastGpsDisciplineAt = 0;

struct UnoGpsReading {
  bool present = false;
  bool fixValid = false;
  bool timeValid = false;
  time_t epoch = 0;
  double latitude = 0;
  double longitude = 0;
  double altitudeM = 0;
  bool altitudeValid = false;
  double hdop = 0;
  bool hdopValid = false;
  uint32_t fixAgeMs = UINT32_MAX;
  uint32_t receivedAt = 0;
  uint32_t satellites = 0;
};

UnoGpsReading unoGps;

uint32_t lastSampleAt = 0;
uint32_t lastWifiAttemptAt = 0;
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
    case TimeSource::Rtc:
      return "rtc";
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

bool systemTimeIsValid() {
  return time(nullptr) >= config::MIN_VALID_UNIX_TIME;
}

void setSystemTime(time_t epoch) {
  timeval value = {epoch, 0};
  settimeofday(&value, nullptr);
}

void writeRtc(time_t epoch, TimeSource source) {
  if (!hasRtc || epoch < config::MIN_VALID_UNIX_TIME) {
    return;
  }
  rtc.adjust(DateTime(static_cast<uint32_t>(epoch)));
  rtcLastSetSource = source;
  addTelemetryEvent("time", "info", "RTC updated from %s",
                    timeSourceName(source));
}

void initializeTimekeeping() {
  hasRtc = config::RTC_ENABLED && rtc.begin(&Wire);
  if (hasRtc && !rtc.lostPower()) {
    const time_t rtcEpoch = rtc.now().unixtime();
    if (rtcEpoch >= config::MIN_VALID_UNIX_TIME) {
      setSystemTime(rtcEpoch);
      timeSource = TimeSource::Rtc;
      addTelemetryEvent("time", "info", "System clock restored from RTC");
    }
  }

  settimeofday_cb([](bool fromSntp) {
    if (fromSntp) {
      ntpSyncArrived = true;
    }
  });
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
}

bool gpsFixIsFresh() {
  return unoGps.present && unoGps.fixValid &&
         unoGps.fixAgeMs <= config::GPS_MAX_FIX_AGE_MS &&
         millis() - unoGps.receivedAt <= config::GPS_RELAY_STALE_MS &&
         unoGps.satellites >= config::GPS_MIN_SATELLITES;
}

bool gpsTimeIsFresh() {
  return unoGps.present && unoGps.timeValid &&
         unoGps.epoch >= config::MIN_VALID_UNIX_TIME &&
         millis() - unoGps.receivedAt <= config::GPS_RELAY_STALE_MS;
}

time_t gpsEpoch() {
  return unoGps.epoch;
}

void pollTimeSources() {
  const uint32_t now = millis();
  const bool validGpsFix = gpsFixIsFresh();
  if (validGpsFix) {
    lastGpsFixAt = now;
  }

  if (validGpsFix && gpsTimeIsFresh()) {
    const bool firstGpsSync = timeSource != TimeSource::Gps;
    const bool disciplineDue =
        lastGpsDisciplineAt == 0 ||
        now - lastGpsDisciplineAt >= config::GPS_DISCIPLINE_INTERVAL_MS;
    if (firstGpsSync || disciplineDue) {
      const time_t epoch = gpsEpoch();
      if (epoch >= config::MIN_VALID_UNIX_TIME) {
        sntp_stop();
        setSystemTime(epoch);
        writeRtc(epoch, TimeSource::Gps);
        lastGpsDisciplineAt = now;
        addTelemetryEvent("time", "info", "System clock disciplined from GPS");
      }
    }
    timeSource = TimeSource::Gps;
  } else if (timeSource == TimeSource::Gps &&
             now - lastGpsFixAt > config::GPS_MAX_FIX_AGE_MS) {
    timeSource = TimeSource::GpsHoldover;
  }

  if (ntpSyncArrived) {
    ntpSyncArrived = false;
    if (timeSource != TimeSource::Gps && systemTimeIsValid()) {
      timeSource = TimeSource::Ntp;
      writeRtc(time(nullptr), TimeSource::Ntp);
      addTelemetryEvent("time", "info",
                        "Initial system clock synchronized from NTP");
    }
    // This station intentionally uses NTP only for the initial sync. GPS takes
    // precedence when it becomes valid; the RTC provides subsequent holdover.
    sntp_stop();
  }
}

String romToString(const DeviceAddress address) {
  char text[17] = {};
  for (uint8_t i = 0; i < 8; ++i) {
    snprintf(text + i * 2, sizeof(text) - i * 2, "%02X", address[i]);
  }
  return String(text);
}

String iso8601Now() {
  const time_t now = time(nullptr);
  if (now < config::MIN_VALID_UNIX_TIME) {
    return String();
  }
  struct tm utc {};
  gmtime_r(&now, &utc);
  char timestamp[25] = {};
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(timestamp);
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED || strlen(WIFI_SSID) == 0) {
    return;
  }

  const uint32_t now = millis();
  if (lastWifiAttemptAt != 0 &&
      now - lastWifiAttemptAt < config::WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastWifiAttemptAt = now;
  ++wifiConnectAttempts;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(config::DEVICE_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  addTelemetryEvent("wifi", "info", "Connecting to WiFi '%s'", WIFI_SSID);
}

void monitorWifiStatus() {
  const wl_status_t status = WiFi.status();
  if (status == previousWifiStatus) {
    return;
  }
  previousWifiStatus = status;
  if (status == WL_CONNECTED) {
    ++wifiConnectEvents;
    addTelemetryEvent("wifi", "info", "Connected; IP=%s RSSI=%d dBm",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    addTelemetryEvent("wifi", "warn", "WiFi status changed to %d",
                      static_cast<int>(status));
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

void updateUnoTelemetry(JsonObject reply) {
  if (reply.isNull()) {
    unoTelemetry.available = false;
    return;
  }
  unoTelemetry.available = true;
  snprintf(unoTelemetry.firmware, sizeof(unoTelemetry.firmware), "%s",
           reply["firmware"] | "unknown");
  unoTelemetry.uptimeMs = reply["uptime_ms"] | 0UL;
  unoTelemetry.freeSramBytes = reply["free_sram_bytes"] | 0;
  unoTelemetry.resetFlags = reply["reset_flags"] | 0;
  unoTelemetry.commandsReceived = reply["commands_received"] | 0UL;
  unoTelemetry.unknownCommands = reply["unknown_commands"] | 0UL;
  unoTelemetry.commandOverflows = reply["command_overflows"] | 0UL;
  unoTelemetry.adcSamplesCompleted = reply["adc_samples_completed"] | 0UL;
  unoTelemetry.lastSampleMs = reply["last_sample_ms"] | 0UL;
  unoTelemetry.loopCount = reply["loop_count"] | 0UL;
  unoTelemetry.gpsCharsProcessed = reply["gps_chars_processed"] | 0UL;
  unoTelemetry.gpsSentencesOk = reply["gps_sentences_ok"] | 0UL;
  unoTelemetry.gpsChecksumFailures = reply["gps_checksum_failures"] | 0UL;
  unoTelemetry.soilPowerSwitched = reply["soil_power_switched"] | false;
}

bool readUnoAdc(uint16_t (&counts)[2], uint32_t &unoSequence) {
  ++unoRequests;
  unoSerial.listen();
  while (unoSerial.available() > 0) {
    unoSerial.read();
  }
  unoSerial.print(F("READ\n"));

  char response[1024] = {};
  size_t length = 0;
  bool overflowed = false;
  const uint32_t startedAt = millis();
  while (millis() - startedAt < config::UNO_RESPONSE_TIMEOUT_MS) {
    while (unoSerial.available() > 0) {
      const char value = static_cast<char>(unoSerial.read());
      if (value == '\n') {
        response[length] = '\0';
        unoLastLatencyMs = millis() - startedAt;
        unoLastResponseBytes = length;
        StaticJsonDocument<1024> reply;
        const DeserializationError error = deserializeJson(reply, response);
        if (overflowed || error || reply["v"].as<uint8_t>() != 1 ||
            !reply.containsKey("a0") || !reply.containsKey("a1")) {
          ++unoInvalidResponses;
          unoTelemetry.available = false;
          addTelemetryEvent("uno", "error", "Invalid response (%u bytes): %.44s",
                            static_cast<unsigned>(length), response);
          return false;
        }

        const uint16_t a0 = reply["a0"].as<uint16_t>();
        const uint16_t a1 = reply["a1"].as<uint16_t>();
        if (a0 > config::UNO_ADC_MAX_COUNTS ||
            a1 > config::UNO_ADC_MAX_COUNTS) {
          ++unoInvalidResponses;
          addTelemetryEvent("uno", "error", "ADC response out of range");
          return false;
        }
        counts[0] = a0;
        counts[1] = a1;
        unoSequence = reply["seq"] | 0UL;

        JsonObject gpsReply = reply["gps"];
        unoGps = UnoGpsReading();
        if (!gpsReply.isNull()) {
          unoGps.present = true;
          unoGps.fixValid = gpsReply["fix_valid"] | false;
          unoGps.timeValid = gpsReply["time_valid"] | false;
          unoGps.satellites = gpsReply["satellites"] | 0UL;
          unoGps.fixAgeMs = gpsReply["fix_age_ms"] | UINT32_MAX;
          unoGps.receivedAt = millis();
          if (unoGps.timeValid) {
            const uint16_t year = gpsReply["year"] | 0;
            const uint8_t month = gpsReply["month"] | 0;
            const uint8_t day = gpsReply["day"] | 0;
            const uint8_t hour = gpsReply["hour"] | 0;
            const uint8_t minute = gpsReply["minute"] | 0;
            const uint8_t second = gpsReply["second"] | 0;
            if (year >= 2021 && month >= 1 && month <= 12 && day >= 1 &&
                day <= 31 && hour <= 23 && minute <= 59 && second <= 60) {
              unoGps.epoch =
                  DateTime(year, month, day, hour, minute, second).unixtime();
            } else {
              unoGps.timeValid = false;
            }
          }
          if (unoGps.fixValid) {
            unoGps.latitude = gpsReply["latitude_deg"] | 0.0;
            unoGps.longitude = gpsReply["longitude_deg"] | 0.0;
          }
          if (gpsReply.containsKey("altitude_m")) {
            unoGps.altitudeValid = true;
            unoGps.altitudeM = gpsReply["altitude_m"].as<double>();
          }
          if (gpsReply.containsKey("hdop")) {
            unoGps.hdopValid = true;
            unoGps.hdop = gpsReply["hdop"].as<double>();
          }
        }
        updateUnoTelemetry(reply["telemetry"].as<JsonObject>());
        ++unoSuccesses;
        return true;
      }
      if (value != '\r' && length + 1 < sizeof(response)) {
        response[length++] = value;
      } else if (value != '\r') {
        overflowed = true;
      }
    }
    delay(1);
    yield();
  }

  ++unoTimeouts;
  unoLastLatencyMs = millis() - startedAt;
  unoLastResponseBytes = length;
  unoTelemetry.available = false;
  addTelemetryEvent("uno", "warn", "Timed out waiting for response (%u bytes)",
                    static_cast<unsigned>(length));
  return false;
}

void addDs18b20Readings(JsonObject sensors) {
  JsonArray probes = sensors.createNestedArray("ds18b20");
  ds18b20.requestTemperatures();
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

    const bool present = OneWire::crc8(address, 7) == address[7] &&
                         ds18b20.isConnected(address);
    probe["present"] = present;
    if (!present) {
      probe["ok"] = false;
      continue;
    }

    const float temperature = ds18b20.getTempC(address);
    if (temperature == DEVICE_DISCONNECTED_C || temperature < -55.0f ||
        temperature > 125.0f) {
      probe["ok"] = false;
    } else {
      probe["ok"] = true;
      probe["temperature_c"] = temperature;
    }
  }
}

void addSht45Reading(JsonObject sensors) {
  JsonObject out = sensors.createNestedObject("sht45");
  if (!hasSht45) {
    out["ok"] = false;
    return;
  }

  sensors_event_t humidity;
  sensors_event_t temperature;
  if (!sht45.getEvent(&humidity, &temperature)) {
    out["ok"] = false;
    return;
  }
  out["ok"] = true;
  out["temperature_c"] = temperature.temperature;
  out["humidity_pct"] = humidity.relative_humidity;
}

void addSoilReadings(JsonObject sensors, bool hasUnoReading,
                     const uint16_t (&counts)[2], uint32_t unoSequence) {
  JsonArray probes = sensors.createNestedArray("soil_moisture");

  for (uint8_t channel = 0; channel < 2; ++channel) {
    JsonObject probe = probes.createNestedObject();
    probe["channel"] = channel;
    probe["sensor_id"] = channel + 1;
    probe["uno_pin"] = channel == 0 ? "A0" : "A1";
    probe["depth_cm"] = config::SOIL_DEPTH_CM[channel];
    probe["sensor_type"] = "resistive_lm393_1p3m";
    probe["adc"] = "arduino_uno_10bit";
    if (!hasUnoReading) {
      probe["ok"] = false;
      continue;
    }

    probe["ok"] = true;
    probe["raw_counts"] = counts[channel];
    probe["voltage_v"] = counts[channel] * config::UNO_ADC_REFERENCE_V /
                         config::UNO_ADC_MAX_COUNTS;
    probe["filter"] = "median";
    probe["sample_count"] = 9;
    probe["uno_sequence"] = unoSequence;

    float percent = 0;
    if (soilPercent(counts[channel], channel, percent)) {
      probe["moisture_pct"] = percent;
    } else {
      probe["moisture_pct"] = nullptr;
    }
  }
}

void addTslReading(JsonObject sensors, const char *name, Tsl2584 &sensor,
                   bool available) {
  JsonObject out = sensors.createNestedObject(name);
  out["address"] = sensor.address();
  Tsl2584Reading reading;
  if (!available || !sensor.read(reading)) {
    out["ok"] = false;
    return;
  }
  out["ok"] = true;
  out["broadband_counts"] = reading.broadbandCounts;
  out["infrared_counts"] = reading.infraredCounts;
  out["visible_counts"] = reading.visibleCounts;
  out["saturated"] = reading.saturated;
}

void addTcs3448Reading(JsonObject sensors) {
  JsonObject out = sensors.createNestedObject("tcs3448");
  if (!hasTcs3448) {
    out["ok"] = false;
    return;
  }

  uint16_t readings[TCS3448_CHANNEL_COUNT] = {};
  if (!tcs3448.readAllChannels(readings)) {
    out["ok"] = false;
    return;
  }

  out["ok"] = true;
  out["f1_405nm"] = readings[TCS3448_CHANNEL_F1];
  out["f2_425nm"] = readings[TCS3448_CHANNEL_F2];
  out["fz_450nm"] = readings[TCS3448_CHANNEL_FZ];
  out["f3_475nm"] = readings[TCS3448_CHANNEL_F3];
  out["f4_515nm"] = readings[TCS3448_CHANNEL_F4];
  out["f5_550nm"] = readings[TCS3448_CHANNEL_F5];
  out["fy_555nm"] = readings[TCS3448_CHANNEL_FY];
  out["fxl_600nm"] = readings[TCS3448_CHANNEL_FXL];
  out["f6_640nm"] = readings[TCS3448_CHANNEL_F6];
  out["f7_690nm"] = readings[TCS3448_CHANNEL_F7];
  out["f8_745nm"] = readings[TCS3448_CHANNEL_F8];
  out["nir_855nm"] = readings[TCS3448_CHANNEL_NIR];
  out["visible"] = readings[TCS3448_CHANNEL_VIS_TL_0];
}

void addTimeAndGpsReading(JsonObject sensors) {
  JsonObject timing = sensors.createNestedObject("timekeeping");
  timing["ok"] = systemTimeIsValid();
  timing["source"] = timeSourceName(timeSource);
  timing["rtc_available"] = hasRtc;
  timing["rtc_last_set_source"] = timeSourceName(rtcLastSetSource);

  JsonObject out = sensors.createNestedObject("gps");
  const bool validFix = gpsFixIsFresh();
  out["fix_valid"] = validFix;
  out["time_valid"] = gpsTimeIsFresh();
  out["relay_present"] = unoGps.present;
  out["satellites"] = unoGps.satellites;
  if (unoGps.hdopValid) {
    out["hdop"] = unoGps.hdop;
  }
  if (validFix) {
    out["latitude_deg"] = unoGps.latitude;
    out["longitude_deg"] = unoGps.longitude;
    out["fix_age_ms"] = unoGps.fixAgeMs;
    if (unoGps.altitudeValid) {
      out["altitude_m"] = unoGps.altitudeM;
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

void addTelemetrySnapshot(JsonObject telemetry) {
  telemetry["version"] = 1;
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
  wifi["connect_attempts"] = wifiConnectAttempts;
  wifi["connect_events"] = wifiConnectEvents;
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

  JsonObject presence = telemetry.createNestedObject("sensor_presence");
  presence["ds18b20_discovered"] = ds18b20.getDeviceCount();
  presence["ds18b20_configured"] = config::DS18B20_COUNT;
  presence["ds18b20_mapped_present"] = configuredDs18b20PresentCount();
  presence["sht45"] = hasSht45;
  presence["tsl2584_sea"] = hasTslSea;
  presence["tsl2584_land"] = hasTslLand;
  presence["tcs3448"] = hasTcs3448;
  presence["ds3231"] = hasRtc;

  JsonObject unoLink = telemetry.createNestedObject("uno_link");
  unoLink["requests"] = unoRequests;
  unoLink["successes"] = unoSuccesses;
  unoLink["timeouts"] = unoTimeouts;
  unoLink["invalid_responses"] = unoInvalidResponses;
  unoLink["last_latency_ms"] = unoLastLatencyMs;
  unoLink["last_response_bytes"] = unoLastResponseBytes;
  unoLink["telemetry_available"] = unoTelemetry.available;

  if (unoTelemetry.available) {
    JsonObject uno = telemetry.createNestedObject("uno");
    uno["firmware"] = unoTelemetry.firmware;
    uno["uptime_ms"] = unoTelemetry.uptimeMs;
    uno["free_sram_bytes"] = unoTelemetry.freeSramBytes;
    uno["reset_flags"] = unoTelemetry.resetFlags;
    uno["commands_received"] = unoTelemetry.commandsReceived;
    uno["unknown_commands"] = unoTelemetry.unknownCommands;
    uno["command_overflows"] = unoTelemetry.commandOverflows;
    uno["adc_samples_completed"] = unoTelemetry.adcSamplesCompleted;
    uno["last_sample_ms"] = unoTelemetry.lastSampleMs;
    uno["loop_count"] = unoTelemetry.loopCount;
    uno["gps_chars_processed"] = unoTelemetry.gpsCharsProcessed;
    uno["gps_sentences_ok"] = unoTelemetry.gpsSentencesOk;
    uno["gps_checksum_failures"] = unoTelemetry.gpsChecksumFailures;
    uno["soil_power_switched"] = unoTelemetry.soilPowerSwitched;
  }

  JsonObject http = telemetry.createNestedObject("http");
  http["attempts"] = postAttempts;
  http["successes"] = postSuccesses;
  http["failures"] = postFailures;
  http["last_status"] = postLastStatus;

  JsonObject clock = telemetry.createNestedObject("timekeeping");
  clock["system_time_valid"] = systemTimeIsValid();
  clock["source"] = timeSourceName(timeSource);
  clock["rtc_last_set_source"] = timeSourceName(rtcLastSetSource);

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

String makePayload() {
  uint16_t unoCounts[2] = {};
  uint32_t unoSequence = 0;
  const bool hasUnoReading = readUnoAdc(unoCounts, unoSequence);
  pollTimeSources();

  DynamicJsonDocument doc(8192);
  doc["schema_version"] = 1;
  doc["device_id"] = config::DEVICE_ID;
  doc["firmware"] = config::FIRMWARE_VERSION;
  doc["sequence"] = sequenceNumber;

  char eventId[64] = {};
  snprintf(eventId, sizeof(eventId), "%s-%08lx-%lu", config::DEVICE_ID,
           static_cast<unsigned long>(bootId),
           static_cast<unsigned long>(sequenceNumber));
  doc["event_id"] = eventId;

  const String observedAt = iso8601Now();
  if (observedAt.length() > 0) {
    doc["observed_at"] = observedAt;
  } else {
    doc["observed_at"] = nullptr;
  }
  doc["uptime_ms"] = millis();
  doc["wifi_rssi_dbm"] =
      WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

  JsonObject sensors = doc.createNestedObject("sensors");
  addDs18b20Readings(sensors);
  addSht45Reading(sensors);
  addSoilReadings(sensors, hasUnoReading, unoCounts, unoSequence);
  addTslReading(sensors, "tsl2584_sea", tslSea, hasTslSea);
  addTslReading(sensors, "tsl2584_land", tslLand, hasTslLand);
  addTcs3448Reading(sensors);
  addTimeAndGpsReading(sensors);
  addTelemetrySnapshot(doc.createNestedObject("telemetry"));

  if (doc.overflowed()) {
    addTelemetryEvent("json", "error", "Telemetry payload exceeded JSON capacity");
    return String();
  }

  String payload;
  serializeJson(doc, payload);
  return payload;
}

template <typename TClient>
bool postWithClient(TClient &client, const String &payload) {
  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  if (!http.begin(client, config::INGEST_URL)) {
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

bool postPayload(const String &payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Not posting: WiFi disconnected"));
    return false;
  }

  const String url(config::INGEST_URL);
  if (url.startsWith("https://")) {
    if (!systemTimeIsValid()) {
      Serial.println(F("Not posting HTTPS: waiting for NTP, GPS, or RTC time"));
      return false;
    }
    BearSSL::WiFiClientSecure client;
    if (certificateStoreReady) {
      client.setCertStore(&certificateStore);
      return postWithClient(client, payload);
    }
#ifdef TLS_ROOT_CA_PEM_OVERRIDE
    const char *rootCa = TLS_ROOT_CA_PEM_OVERRIDE;
#else
    const char *rootCa = GTS_ROOT_R4_PEM;
#endif
    if (strlen(rootCa) > 0) {
      BearSSL::X509List trustAnchor(rootCa);
      client.setTrustAnchors(&trustAnchor);
      return postWithClient(client, payload);
    }
#if ALLOW_INSECURE_TLS
    Serial.println(F("WARNING: HTTPS certificate verification is disabled"));
    client.setInsecure();
    return postWithClient(client, payload);
#else
    Serial.println(F("Not posting: no TLS root CA is configured"));
    return false;
#endif
  }

  WiFiClient client;
  return postWithClient(client, payload);
}

bool readyToSampleAndSend() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  return strncmp(config::INGEST_URL, "https://", 8) != 0 ||
         systemTimeIsValid();
}

void initializeTlsTrustStore() {
  if (!LittleFS.begin()) {
    addTelemetryEvent("tls", "warn",
                      "LittleFS mount failed; using compiled fallback");
    return;
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
  Serial.println(F("I2C scan:"));
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
  addTelemetryEvent("i2c", "info", "Scan found %u device(s)",
                    i2cAddressCount);
}

void initializeSensors() {
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(true);

  hasSht45 = sht45.begin(&Wire);
  if (hasSht45) {
    sht45.setPrecision(SHT4X_HIGH_PRECISION);
    sht45.setHeater(SHT4X_NO_HEATER);
  }

  hasTslSea = tslSea.begin(Wire);
  hasTslLand = tslLand.begin(Wire);
  hasTcs3448 = tcs3448.begin();
  if (hasTcs3448) {
    hasTcs3448 = tcs3448.setGain(TCS3448_GAIN_64X) &&
                 tcs3448.setATIME(29) && tcs3448.setASTEP(599) &&
                 tcs3448.setSMUXMode(TCS3448_SMUX_18CH);
  }
}

void printSensorStatus() {
  const char *rtcStatus =
      config::RTC_ENABLED ? (hasRtc ? "ok" : "missing") : "disabled";
  addTelemetryEvent(
      "sensors", "info",
      "DS18B20=%u/%u SHT45=%s Uno=serial TSL-Sea=%s TSL-Land=%s TCS=%s RTC=%s",
      configuredDs18b20PresentCount(), config::DS18B20_COUNT,
      hasSht45 ? "ok" : "missing", hasTslSea ? "ok" : "missing",
      hasTslLand ? "ok" : "missing", hasTcs3448 ? "ok" : "missing",
      rtcStatus);
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
  unoSerial.begin(config::UNO_SERIAL_BAUD);
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
  connectWifi();
  monitorWifiStatus();
  pollTimeSources();

  // Do not consume a sequence number or take sensor measurements until setup
  // is complete and the network/TLS prerequisites allow an immediate send.
  if (!readyToSampleAndSend()) {
    delay(10);
    return;
  }

  const uint32_t now = millis();
  if (lastSampleAt == 0 || now - lastSampleAt >= config::SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    ++sequenceNumber;

    const String payload = makePayload();
    if (payload.length() == 0) {
      delay(10);
      return;
    }
    Serial.println(payload);
    postPayload(payload);
  }

  delay(10);
}
