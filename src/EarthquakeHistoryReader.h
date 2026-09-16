#pragma once

#include "EarthquakeHistoryService.h"
#include "ReverseNdjsonLine.h"

class EarthquakeHistoryReader {
 public:
  static constexpr size_t MAX_RECORDS = 20;
  enum class Status { Loading, Available, Empty, Unavailable, Busy, Error };

  void begin(EarthquakeHistoryService* history, bool storageAvailable);
  void refresh();
  void close() { active_ = false; }
  void next();
  void loop();
  bool consumeChanged();
  Status status() const { return status_; }
  size_t count() const { return count_; }
  size_t selected() const { return selected_; }
  const char* json() const { return selectedJson_; }

 private:
  struct RecordPosition {
    EarthquakeHistoryKind kind = EarthquakeHistoryKind::Eew;
    uint32_t generation = 0;
    size_t offset = 0;
    uint16_t length = 0;
  };
  struct Cursor {
    EarthquakeHistoryService::GenerationInfo generations[
        EarthquakeHistoryService::MAX_GENERATIONS];
    int generationIndex = -1;
    size_t position = 0;
    bool initialized = false;
    bool exhausted = false;
    bool ready = false;
    ReverseNdjsonLine<EarthquakeHistoryService::MAX_RECORD_BYTES> line;
    RecordPosition record;
    char json[EarthquakeHistoryService::MAX_RECORD_BYTES] = {};
    char receivedAt[25] = {};
  };
  void setStatus(Status status);
  void advanceCursor(size_t index);
  bool acceptLine(Cursor& cursor, size_t index, size_t offset);
  void loadSelected();

  EarthquakeHistoryService* history_ = nullptr;
  bool storageAvailable_ = false;
  bool active_ = false;
  bool building_ = false;
  bool selectionPending_ = false;
  bool changed_ = false;
  bool readError_ = false;
  Status status_ = Status::Unavailable;
  Cursor cursors_[2];
  RecordPosition records_[MAX_RECORDS];
  size_t count_ = 0;
  size_t selected_ = 0;
  char selectedJson_[EarthquakeHistoryService::MAX_RECORD_BYTES] = {};
};
