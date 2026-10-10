#pragma once

#include <Arduino.h>
#include <FS.h>

namespace offline {

constexpr uint32_t RECORD_MAGIC = 0x54475131UL;  // "TGQ1"
constexpr uint16_t RECORD_FORMAT_VERSION = 1;
constexpr uint16_t RECORD_BYTES = 256;
constexpr uint16_t RECORD_CAPACITY = 2048;
constexpr size_t QUEUE_FILE_BYTES =
    static_cast<size_t>(RECORD_BYTES) * RECORD_CAPACITY;
constexpr char QUEUE_PATH[] = "/offline-readings.q";

enum ReadingFlag : uint32_t {
  DS1_PRESENT = 1UL << 0,
  DS2_PRESENT = 1UL << 1,
  DS3_PRESENT = 1UL << 2,
  DS4_PRESENT = 1UL << 3,
  DS1_OK = 1UL << 4,
  DS2_OK = 1UL << 5,
  DS3_OK = 1UL << 6,
  DS4_OK = 1UL << 7,
  SHT45_OK = 1UL << 8,
  UNO_OK = 1UL << 9,
  TSL_SEA_OK = 1UL << 10,
  TSL_LAND_OK = 1UL << 11,
  TCS3448_OK = 1UL << 12,
  RTC_AVAILABLE = 1UL << 13,
  GPS_RELAY_PRESENT = 1UL << 14,
  GPS_FIX_VALID = 1UL << 15,
  GPS_TIME_VALID = 1UL << 16,
  TSL_SEA_SATURATED = 1UL << 17,
  TSL_LAND_SATURATED = 1UL << 18,
  GPS_HDOP_VALID = 1UL << 19,
  GPS_ALTITUDE_VALID = 1UL << 20,
  SYSTEM_TIME_VALID = 1UL << 21,
};

#pragma pack(push, 1)
struct ReadingRecord {
  uint32_t magic = RECORD_MAGIC;
  uint16_t formatVersion = RECORD_FORMAT_VERSION;
  uint16_t recordBytes = RECORD_BYTES;
  uint32_t storageId = 0;
  uint32_t bootId = 0;
  uint32_t sequence = 0;
  uint32_t observedEpoch = 0;
  uint32_t uptimeMs = 0;
  uint32_t rtcEpoch = 0;
  uint32_t gpsEpoch = 0;
  uint32_t unoSequence = 0;
  uint32_t gpsFixAgeMs = UINT32_MAX;
  uint32_t flags = 0;
  int32_t gpsLatitudeE7 = 0;
  int32_t gpsLongitudeE7 = 0;
  int32_t gpsAltitudeMm = 0;
  char firmware[12] = {};
  int8_t wifiRssiDbm = -127;
  uint8_t timeSource = 0;
  uint8_t rtcLastSetSource = 0;
  uint8_t gpsSatellites = 0;
  uint16_t gpsHdopCenti = 0;
  int16_t dsTemperatureCenti[4] = {};
  int16_t shtTemperatureCenti = 0;
  uint16_t shtHumidityCenti = 0;
  uint16_t soilCounts[2] = {};
  uint16_t tslSea[3] = {};
  uint16_t tslLand[3] = {};
  uint16_t tcs3448[13] = {};
  uint32_t unoRequests = 0;
  uint32_t unoSuccesses = 0;
  uint32_t unoTimeouts = 0;
  uint32_t unoInvalidResponses = 0;
  uint16_t unoLastLatencyMs = 0;
  uint16_t unoLastResponseBytes = 0;
  uint8_t reserved[100] = {};
  uint32_t crc32 = 0;
};
#pragma pack(pop)

static_assert(sizeof(ReadingRecord) == RECORD_BYTES,
              "Offline reading record must remain exactly 256 bytes");

class Queue {
 public:
  bool begin(fs::FS &filesystem);
  bool enqueue(ReadingRecord &record);
  bool peek(ReadingRecord &record, uint16_t offset = 0);
  bool acknowledge(uint16_t count);

  bool ready() const { return ready_; }
  uint16_t count() const { return count_; }
  uint16_t capacity() const { return RECORD_CAPACITY; }
  uint32_t dropped() const { return dropped_; }
  uint32_t replayed() const { return replayed_; }
  uint32_t corruptRecords() const { return corruptRecords_; }
  size_t fileBytes() const { return ready_ ? QUEUE_FILE_BYTES : 0; }

 private:
  bool prepareFile();
  bool scan();
  bool readSlot(uint16_t index, ReadingRecord &record);
  bool writeSlot(uint16_t index, const ReadingRecord &record);
  bool clearSlot(uint16_t index);
  bool valid(const ReadingRecord &record) const;
  bool findNextValid(uint16_t start, uint16_t &index,
                     ReadingRecord *record = nullptr);
  bool discardOldest(bool replayed);
  static uint32_t calculateCrc(const ReadingRecord &record);

  fs::FS *filesystem_ = nullptr;
  bool ready_ = false;
  uint16_t headIndex_ = 0;
  uint16_t nextWriteIndex_ = 0;
  uint16_t count_ = 0;
  uint32_t nextStorageId_ = 1;
  uint32_t dropped_ = 0;
  uint32_t replayed_ = 0;
  uint32_t corruptRecords_ = 0;
};

}  // namespace offline
