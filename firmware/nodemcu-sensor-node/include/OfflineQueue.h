#pragma once

#include <Arduino.h>
#include <FS.h>
#include <stddef.h>

namespace offline {

constexpr uint32_t RECORD_MAGIC = 0x54475134UL;  // "TGQ4"
constexpr uint32_t LEGACY_RECORD_MAGIC_V1 = 0x54475131UL;  // "TGQ1"
constexpr uint32_t LEGACY_RECORD_MAGIC_V2 = 0x54475132UL;  // "TGQ2"
constexpr uint32_t LEGACY_RECORD_MAGIC_V3 = 0x54475133UL;  // "TGQ3"
constexpr uint16_t RECORD_FORMAT_VERSION = 4;
constexpr uint16_t RECORD_BYTES = 256;
constexpr uint16_t RECORD_CAPACITY = 1536;
constexpr size_t QUEUE_FILE_BYTES =
    static_cast<size_t>(RECORD_BYTES) * RECORD_CAPACITY;
// Reuse the original allocation. The new magic/version makes every older
// record invalid, so fields from an incompatible packed layout are never
// decoded while the fixed-size file can be safely overwritten in place.
constexpr char QUEUE_PATH[] = "/offline-readings.q";
constexpr char LEGACY_V2_QUEUE_PATH[] = "/offline-readings-v2.q";

enum ReadingFlag : uint32_t {
  DS1_PRESENT = 1UL << 0,
  DS2_PRESENT = 1UL << 1,
  DS3_PRESENT = 1UL << 2,
  DS4_PRESENT = 1UL << 3,
  DS1_OK = 1UL << 4,
  DS2_OK = 1UL << 5,
  DS3_OK = 1UL << 6,
  DS4_OK = 1UL << 7,
  ADS1115_PRESENT = 1UL << 8,
  ADS1115_OK = 1UL << 9,
  GPS_PRESENT = 1UL << 10,
  GPS_FIX_VALID = 1UL << 11,
  GPS_TIME_VALID = 1UL << 12,
  GPS_HDOP_VALID = 1UL << 13,
  GPS_ALTITUDE_VALID = 1UL << 14,
  SYSTEM_TIME_VALID = 1UL << 15,
  SHT45_OK = 1UL << 16,
  TSL_SEA_OK = 1UL << 17,
  TSL_LAND_OK = 1UL << 18,
  TCS3448_OK = 1UL << 19,
  TSL_SEA_SATURATED = 1UL << 20,
  TSL_LAND_SATURATED = 1UL << 21,
};

#pragma pack(push, 1)
struct alignas(4) ReadingRecord {
  uint32_t magic = RECORD_MAGIC;
  uint16_t formatVersion = RECORD_FORMAT_VERSION;
  uint16_t recordBytes = RECORD_BYTES;
  uint32_t storageId = 0;
  uint32_t bootId = 0;
  uint32_t sequence = 0;
  uint32_t observedEpoch = 0;
  uint32_t uptimeMs = 0;
  uint32_t gpsEpoch = 0;
  uint32_t gpsFixAgeMs = UINT32_MAX;
  uint32_t flags = 0;
  int32_t gpsLatitudeE7 = 0;
  int32_t gpsLongitudeE7 = 0;
  int32_t gpsAltitudeMm = 0;
  uint32_t gpsCharsProcessed = 0;
  uint32_t gpsSentencesOk = 0;
  uint32_t gpsChecksumFailures = 0;
  uint32_t adsReads = 0;
  uint32_t adsFailures = 0;
  char firmware[12] = {};
  int8_t wifiRssiDbm = -127;
  uint8_t timeSource = 0;
  uint8_t gpsSatellites = 0;
  // Keep the entire 16-bit sensor region naturally aligned. Format 3 omitted
  // this byte and could fault when a library bound an odd-offset field by
  // reference on ESP8266.
  uint8_t alignmentPadding = 0;
  uint16_t gpsHdopCenti = 0;
  int16_t dsTemperatureCenti[4] = {};
  int16_t soilCounts[2] = {};
  uint16_t soilMillivolts[2] = {};
  int16_t shtTemperatureCenti = 0;
  uint16_t shtHumidityCenti = 0;
  uint16_t tslSea[3] = {};
  uint16_t tslLand[3] = {};
  uint16_t tcs3448[13] = {};
  // Format 4 intentionally starts a new binary contract.
  uint8_t reserved[104] = {};
  uint32_t crc32 = 0;
};
#pragma pack(pop)

static_assert(sizeof(ReadingRecord) == RECORD_BYTES,
              "Offline reading record must remain exactly 256 bytes");
static_assert(alignof(ReadingRecord) >= 4,
              "Reading records must have an aligned base address");

// ESP8266 raises exception 9 for unaligned half-word and word accesses. Check
// every multi-byte member, not just the fields involved in a known crash, so a
// future binary-format edit fails at compile time instead of in the field.
#define OFFLINE_ASSERT_MEMBER_ALIGNED(member)                              \
  static_assert(offsetof(ReadingRecord, member) %                          \
                        alignof(decltype(ReadingRecord::member)) == 0,      \
                "Unaligned offline record member: " #member)
OFFLINE_ASSERT_MEMBER_ALIGNED(magic);
OFFLINE_ASSERT_MEMBER_ALIGNED(formatVersion);
OFFLINE_ASSERT_MEMBER_ALIGNED(recordBytes);
OFFLINE_ASSERT_MEMBER_ALIGNED(storageId);
OFFLINE_ASSERT_MEMBER_ALIGNED(bootId);
OFFLINE_ASSERT_MEMBER_ALIGNED(sequence);
OFFLINE_ASSERT_MEMBER_ALIGNED(observedEpoch);
OFFLINE_ASSERT_MEMBER_ALIGNED(uptimeMs);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsEpoch);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsFixAgeMs);
OFFLINE_ASSERT_MEMBER_ALIGNED(flags);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsLatitudeE7);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsLongitudeE7);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsAltitudeMm);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsCharsProcessed);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsSentencesOk);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsChecksumFailures);
OFFLINE_ASSERT_MEMBER_ALIGNED(adsReads);
OFFLINE_ASSERT_MEMBER_ALIGNED(adsFailures);
OFFLINE_ASSERT_MEMBER_ALIGNED(gpsHdopCenti);
OFFLINE_ASSERT_MEMBER_ALIGNED(dsTemperatureCenti);
OFFLINE_ASSERT_MEMBER_ALIGNED(soilCounts);
OFFLINE_ASSERT_MEMBER_ALIGNED(soilMillivolts);
OFFLINE_ASSERT_MEMBER_ALIGNED(shtTemperatureCenti);
OFFLINE_ASSERT_MEMBER_ALIGNED(shtHumidityCenti);
OFFLINE_ASSERT_MEMBER_ALIGNED(tslSea);
OFFLINE_ASSERT_MEMBER_ALIGNED(tslLand);
OFFLINE_ASSERT_MEMBER_ALIGNED(tcs3448);
OFFLINE_ASSERT_MEMBER_ALIGNED(crc32);
#undef OFFLINE_ASSERT_MEMBER_ALIGNED

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
