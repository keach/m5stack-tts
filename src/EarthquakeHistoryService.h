#pragma once

#include <Arduino.h>

enum class EarthquakeHistoryKind : uint8_t {
  Eew,
  Earthquake,
};

class EarthquakeHistoryService {
 public:
  static constexpr size_t MAX_GENERATIONS = 10;
  static constexpr uint16_t RECORDS_PER_GENERATION = 500;
  static constexpr size_t MAX_RECORD_BYTES = 768;

  struct GenerationInfo {
    uint32_t generation = 0;
    uint16_t records = 0;
    size_t bytes = 0;
    bool damagedTail = false;
  };

  void begin(bool storageAvailable);
  bool enqueue(EarthquakeHistoryKind kind, const char* json, size_t length);
  void loop();

  size_t generationCount(EarthquakeHistoryKind kind) const;
  const GenerationInfo* generationAt(EarthquakeHistoryKind kind,
                                     size_t index) const;
  bool resolvePath(EarthquakeHistoryKind kind, uint32_t generation,
                   char* path, size_t capacity) const;
  void appendWebSectionLocked(String& html) const;

 private:
  static constexpr size_t QUEUE_CAPACITY = 4;
  static constexpr uint8_t WRITE_RETRY_LIMIT = 3;
  static constexpr unsigned long RETRY_INTERVAL_MS = 1000;

  struct PendingRecord {
    EarthquakeHistoryKind kind = EarthquakeHistoryKind::Eew;
    uint16_t length = 0;
    uint8_t attempts = 0;
    unsigned long retryAt = 0;
    char json[MAX_RECORD_BYTES] = {};
  };

  struct HistoryState {
    GenerationInfo generations[MAX_GENERATIONS] = {};
    size_t count = 0;
    bool cleanupBlocked = false;
  };

  static const char* prefix(EarthquakeHistoryKind kind);
  static const char* label(EarthquakeHistoryKind kind);
  static bool parseGeneration(const char* name, EarthquakeHistoryKind kind,
                              uint32_t* generation);
  static void makePath(EarthquakeHistoryKind kind, uint32_t generation,
                       char* path, size_t capacity);
  static void scanFile(const char* path, GenerationInfo* info);
  static void appendEscapedHtml(String& html, const char* text);
  static bool appendRecordRow(String& html, EarthquakeHistoryKind kind,
                              const char* json);

  HistoryState& state(EarthquakeHistoryKind kind);
  const HistoryState& state(EarthquakeHistoryKind kind) const;
  void scanGenerations(EarthquakeHistoryKind kind);
  bool appendRecord(PendingRecord& record);
  bool createNextGeneration(EarthquakeHistoryKind kind);
  void appendKindWebSectionLocked(String& html,
                                  EarthquakeHistoryKind kind) const;
  void appendLatestRowsLocked(String& html, EarthquakeHistoryKind kind,
                              size_t maximumRows) const;
  size_t appendLatestRowsFromFileLocked(String& html,
                                        EarthquakeHistoryKind kind,
                                        const char* path,
                                        size_t maximumRows) const;

  HistoryState eewState_;
  HistoryState earthquakeState_;
  PendingRecord queue_[QUEUE_CAPACITY];
  size_t queueHead_ = 0;
  size_t queueSize_ = 0;
  bool storageAvailable_ = false;
};
