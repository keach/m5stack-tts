#include "EarthquakeHistoryReader.h"

#include <ArduinoJson.h>
#include <SD.h>

#include "SdCardLock.h"

namespace {
EarthquakeHistoryKind kindAt(size_t index) {
  return index == 0 ? EarthquakeHistoryKind::Eew
                    : EarthquakeHistoryKind::Earthquake;
}
}

void EarthquakeHistoryReader::begin(EarthquakeHistoryService* history,
                                    bool storageAvailable) {
  history_ = history;
  storageAvailable_ = storageAvailable;
}

void EarthquakeHistoryReader::setStatus(Status status) {
  if (status_ != status) changed_ = true;
  status_ = status;
}

bool EarthquakeHistoryReader::consumeChanged() {
  const bool changed = changed_;
  changed_ = false;
  return changed;
}

void EarthquakeHistoryReader::refresh() {
  active_ = true;
  count_ = selected_ = 0;
  selectedJson_[0] = '\0';
  readError_ = false;
  selectionPending_ = false;
  changed_ = true;
  if (!storageAvailable_ || !history_) {
    building_ = false;
    setStatus(Status::Unavailable);
    return;
  }
  for (size_t index = 0; index < 2; ++index) {
    Cursor& cursor = cursors_[index];
    cursor = Cursor();
    const size_t generations = history_->generationCount(kindAt(index));
    for (size_t generation = 0; generation < generations; ++generation) {
      cursor.generations[generation] =
          *history_->generationAt(kindAt(index), generation);
    }
    cursor.generationIndex = static_cast<int>(generations) - 1;
    cursor.exhausted = generations == 0;
  }
  building_ = true;
  setStatus(Status::Loading);
}

bool EarthquakeHistoryReader::acceptLine(Cursor& cursor, size_t index,
                                        size_t offset) {
  JsonDocument document;
  if (deserializeJson(document, cursor.json) ||
      strcmp(document["kind"] | "", index == 0 ? "eew" : "earthquake") != 0) {
    readError_ = true;
    Serial.println("Invalid earthquake history line skipped.");
    return false;
  }
  cursor.record.kind = kindAt(index);
  cursor.record.generation =
      cursor.generations[cursor.generationIndex].generation;
  cursor.record.offset = offset;
  cursor.record.length = strlen(cursor.json);
  strlcpy(cursor.receivedAt, document["received_at"] | "",
          sizeof(cursor.receivedAt));
  cursor.ready = true;
  return true;
}

void EarthquakeHistoryReader::advanceCursor(size_t index) {
  Cursor& cursor = cursors_[index];
  if (cursor.ready || cursor.exhausted) return;
  SdCardGuard guard(0);
  if (!guard.locked()) {
    setStatus(Status::Busy);
    return;
  }
  setStatus(Status::Loading);
  if (cursor.generationIndex < 0) {
    cursor.exhausted = true;
    return;
  }
  char path[48];
  const auto& generation = cursor.generations[cursor.generationIndex];
  if (!history_->resolvePath(kindAt(index), generation.generation, path,
                             sizeof(path))) {
    refresh();
    return;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    readError_ = true;
    Serial.printf("Unable to read earthquake history: %s\n", path);
    --cursor.generationIndex;
    cursor.initialized = false;
    return;
  }
  if (!cursor.initialized) {
    // Freeze each generation at the size observed when the view was opened.
    cursor.position = min(generation.bytes, file.size());
    bool terminated = false;
    if (cursor.position > 0 && file.seek(cursor.position - 1)) {
      terminated = file.read() == '\n';
    }
    cursor.line.reset(terminated);
    cursor.initialized = true;
  }
  uint8_t buffer[256];
  size_t budget = 2048;
  while (cursor.position > 0 && budget > 0 && !cursor.ready) {
    const size_t chunk = min(cursor.position, sizeof(buffer));
    const size_t start = cursor.position - chunk;
    if (!file.seek(start) || file.read(buffer, chunk) != chunk) {
      readError_ = true;
      cursor.position = 0;
      cursor.line.reset(false);
      Serial.printf("Earthquake history read failed: %s\n", path);
      break;
    }
    for (size_t offset = chunk; offset > 0; --offset) {
      --cursor.position;
      --budget;
      const char value = static_cast<char>(buffer[offset - 1]);
      const bool complete = cursor.line.consume(value, cursor.json);
      if (value == '\n') {
        if (complete) acceptLine(cursor, index, cursor.position + 1);
      }
      if (cursor.ready || budget == 0) break;
    }
  }
  if (cursor.position == 0 && !cursor.ready) {
    if (cursor.line.finish(cursor.json)) acceptLine(cursor, index, 0);
    if (!cursor.ready) {
      --cursor.generationIndex;
      cursor.initialized = false;
    }
  }
  file.close();
}

void EarthquakeHistoryReader::next() {
  // Keep navigation available after an I/O failure. With a single record,
  // wrapping also provides an explicit retry without a busy retry loop.
  if (building_ || count_ == 0 ||
      (status_ != Status::Available && status_ != Status::Error &&
       status_ != Status::Busy)) return;
  selected_ = (selected_ + 1) % count_;
  selectionPending_ = true;
  loadSelected();
}

void EarthquakeHistoryReader::loadSelected() {
  if (count_ == 0) return;
  SdCardGuard guard(0);
  if (!guard.locked()) {
    setStatus(Status::Busy);
    return;
  }
  const RecordPosition& record = records_[selected_];
  char path[48];
  if (!history_->resolvePath(record.kind, record.generation, path,
                             sizeof(path))) {
    refresh();
    return;
  }
  File file = SD.open(path, FILE_READ);
  if (!file || !file.seek(record.offset) ||
      file.read(reinterpret_cast<uint8_t*>(selectedJson_), record.length) !=
          record.length) {
    if (file) file.close();
    selectionPending_ = false;
    selectedJson_[0] = '\0';
    setStatus(Status::Error);
    Serial.printf("Selected earthquake history could not be read: %s\n", path);
    return;
  }
  file.close();
  selectedJson_[record.length] = '\0';
  selectionPending_ = false;
  changed_ = true;
  setStatus(Status::Available);
}

void EarthquakeHistoryReader::loop() {
  if (!active_) return;
  if (selectionPending_) {
    loadSelected();
    return;
  }
  if (!building_) {
    if (count_ > 0) {
      char path[48];
      const RecordPosition& record = records_[selected_];
      if (!history_->resolvePath(record.kind, record.generation, path,
                                 sizeof(path))) refresh();
    }
    return;
  }
  for (size_t index = 0; index < 2; ++index) {
    if (!cursors_[index].ready && !cursors_[index].exhausted) {
      advanceCursor(index);
      return;
    }
  }
  if (count_ >= MAX_RECORDS ||
      (cursors_[0].exhausted && cursors_[1].exhausted)) {
    building_ = false;
    if (count_ == 0) setStatus(readError_ ? Status::Error : Status::Empty);
    else {
      selectionPending_ = true;
      loadSelected();
    }
    return;
  }
  size_t newest = 0;
  if (!cursors_[0].ready ||
      (cursors_[1].ready &&
       strcmp(cursors_[1].receivedAt, cursors_[0].receivedAt) > 0)) newest = 1;
  records_[count_++] = cursors_[newest].record;
  cursors_[newest].ready = false;
  // A BOF record has no preceding delimiter; move to the previous generation.
  if (cursors_[newest].position == 0) {
    --cursors_[newest].generationIndex;
    cursors_[newest].initialized = false;
  }
}
