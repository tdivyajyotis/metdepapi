#include "OfflineQueue.h"

#include <stddef.h>
#include <string.h>

namespace offline {
namespace {

uint32_t crc32Bytes(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return ~crc;
}

}  // namespace

uint32_t Queue::calculateCrc(const ReadingRecord &record) {
  return crc32Bytes(reinterpret_cast<const uint8_t *>(&record),
                    offsetof(ReadingRecord, crc32));
}

bool Queue::valid(const ReadingRecord &record) const {
  return record.magic == RECORD_MAGIC &&
         record.formatVersion == RECORD_FORMAT_VERSION &&
         record.recordBytes == RECORD_BYTES &&
         record.crc32 == calculateCrc(record);
}

bool Queue::prepareFile() {
  File existing = filesystem_->open(QUEUE_PATH, "r");
  if (existing) {
    const size_t size = existing.size();
    existing.close();
    if (size == QUEUE_FILE_BYTES) {
      return true;
    }
    filesystem_->remove(QUEUE_PATH);
  }

  File file = filesystem_->open(QUEUE_PATH, "w");
  if (!file) {
    return false;
  }
  uint8_t empty[RECORD_BYTES] = {};
  for (uint16_t slot = 0; slot < RECORD_CAPACITY; ++slot) {
    if (file.write(empty, sizeof(empty)) != sizeof(empty)) {
      file.close();
      filesystem_->remove(QUEUE_PATH);
      return false;
    }
    if ((slot & 0x1F) == 0) {
      yield();
    }
  }
  file.flush();
  file.close();
  return true;
}

bool Queue::readSlot(uint16_t index, ReadingRecord &record) {
  if (filesystem_ == nullptr) {
    return false;
  }
  File file = filesystem_->open(QUEUE_PATH, "r");
  if (!file || !file.seek(static_cast<uint32_t>(index) * RECORD_BYTES,
                          SeekSet)) {
    if (file) {
      file.close();
    }
    return false;
  }
  const size_t bytes =
      file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record));
  file.close();
  return bytes == sizeof(record);
}

bool Queue::writeSlot(uint16_t index, const ReadingRecord &record) {
  File file = filesystem_->open(QUEUE_PATH, "r+");
  if (!file || !file.seek(static_cast<uint32_t>(index) * RECORD_BYTES,
                          SeekSet)) {
    if (file) {
      file.close();
    }
    return false;
  }
  const size_t bytes =
      file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  file.flush();
  file.close();
  return bytes == sizeof(record);
}

bool Queue::clearSlot(uint16_t index) {
  File file = filesystem_->open(QUEUE_PATH, "r+");
  if (!file || !file.seek(static_cast<uint32_t>(index) * RECORD_BYTES,
                          SeekSet)) {
    if (file) {
      file.close();
    }
    return false;
  }
  const uint32_t clearedMagic = 0;
  const size_t bytes = file.write(
      reinterpret_cast<const uint8_t *>(&clearedMagic), sizeof(clearedMagic));
  file.flush();
  file.close();
  return bytes == sizeof(clearedMagic);
}

bool Queue::scan() {
  count_ = 0;
  corruptRecords_ = 0;
  uint32_t oldestId = UINT32_MAX;
  uint32_t newestId = 0;
  uint16_t newestIndex = 0;
  File file = filesystem_->open(QUEUE_PATH, "r");
  if (!file) {
    return false;
  }
  ReadingRecord record;
  for (uint16_t index = 0; index < RECORD_CAPACITY; ++index) {
    if (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) !=
        sizeof(record)) {
      file.close();
      return false;
    }
    if (valid(record)) {
      ++count_;
      if (record.storageId < oldestId) {
        oldestId = record.storageId;
        headIndex_ = index;
      }
      if (record.storageId >= newestId) {
        newestId = record.storageId;
        newestIndex = index;
      }
    } else if (record.magic != 0) {
      ++corruptRecords_;
    }
    if ((index & 0x1F) == 0) {
      yield();
    }
  }
  file.close();
  if (count_ == 0) {
    headIndex_ = 0;
    nextWriteIndex_ = 0;
    nextStorageId_ = 1;
  } else {
    nextWriteIndex_ = (newestIndex + 1) % RECORD_CAPACITY;
    nextStorageId_ = newestId + 1;
  }
  return true;
}

bool Queue::begin(fs::FS &filesystem) {
  filesystem_ = &filesystem;
  if (!prepareFile() || !scan()) {
    filesystem_ = nullptr;
    return false;
  }
  ready_ = true;
  return true;
}

bool Queue::findNextValid(uint16_t start, uint16_t &index,
                          ReadingRecord *recordOut) {
  ReadingRecord record;
  for (uint16_t offset = 0; offset < RECORD_CAPACITY; ++offset) {
    const uint16_t candidate = (start + offset) % RECORD_CAPACITY;
    if (!readSlot(candidate, record)) {
      return false;
    }
    if (valid(record)) {
      index = candidate;
      if (recordOut != nullptr) {
        *recordOut = record;
      }
      return true;
    }
    yield();
  }
  return false;
}

bool Queue::discardOldest(bool wasReplayed) {
  if (count_ == 0) {
    return true;
  }
  uint16_t index = 0;
  if (!findNextValid(headIndex_, index) || !clearSlot(index)) {
    return false;
  }
  --count_;
  if (wasReplayed) {
    ++replayed_;
  } else {
    ++dropped_;
  }
  headIndex_ = (index + 1) % RECORD_CAPACITY;
  if (count_ > 0) {
    uint16_t next = 0;
    if (!findNextValid(headIndex_, next)) {
      return false;
    }
    headIndex_ = next;
  }
  return true;
}

bool Queue::enqueue(ReadingRecord &record) {
  if (!ready_) {
    return false;
  }
  if (count_ == RECORD_CAPACITY && !discardOldest(false)) {
    return false;
  }

  ReadingRecord existing;
  uint16_t slot = nextWriteIndex_;
  bool found = false;
  for (uint16_t offset = 0; offset < RECORD_CAPACITY; ++offset) {
    slot = (nextWriteIndex_ + offset) % RECORD_CAPACITY;
    if (!readSlot(slot, existing)) {
      return false;
    }
    if (!valid(existing)) {
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }

  record.magic = RECORD_MAGIC;
  record.formatVersion = RECORD_FORMAT_VERSION;
  record.recordBytes = RECORD_BYTES;
  record.storageId = nextStorageId_++;
  record.crc32 = calculateCrc(record);
  if (!writeSlot(slot, record)) {
    return false;
  }
  if (count_ == 0) {
    headIndex_ = slot;
  }
  ++count_;
  nextWriteIndex_ = (slot + 1) % RECORD_CAPACITY;
  return true;
}

bool Queue::peek(ReadingRecord &record, uint16_t offset) {
  if (!ready_ || offset >= count_) {
    return false;
  }
  uint16_t seen = 0;
  ReadingRecord candidate;
  for (uint16_t slotOffset = 0; slotOffset < RECORD_CAPACITY; ++slotOffset) {
    const uint16_t slot = (headIndex_ + slotOffset) % RECORD_CAPACITY;
    if (!readSlot(slot, candidate)) {
      return false;
    }
    if (!valid(candidate)) {
      continue;
    }
    if (seen++ == offset) {
      record = candidate;
      return true;
    }
  }
  return false;
}

bool Queue::acknowledge(uint16_t count) {
  if (!ready_ || count > count_) {
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    if (!discardOldest(true)) {
      return false;
    }
  }
  return true;
}

}  // namespace offline
