#include "EarthquakeHistoryService.h"

#include <ArduinoJson.h>
#include <SD.h>

#include "SdCardLock.h"

namespace {
constexpr char EEW_PREFIX[] = "eew_history_";
constexpr char EARTHQUAKE_PREFIX[] = "earthquake_history_";
constexpr char SUFFIX[] = ".ndjson";

bool generationLess(const EarthquakeHistoryService::GenerationInfo& left,
                    const EarthquakeHistoryService::GenerationInfo& right) {
  return left.generation < right.generation;
}
}  // namespace

void EarthquakeHistoryService::begin(bool storageAvailable) {
  storageAvailable_ = storageAvailable;
  if (!storageAvailable_) {
    Serial.println("Earthquake history disabled because microSD is unavailable.");
    return;
  }
  SdCardGuard guard(pdMS_TO_TICKS(1000));
  if (!guard.locked()) {
    storageAvailable_ = false;
    Serial.println("Earthquake history initialization failed: microSD is busy.");
    return;
  }
  scanGenerations(EarthquakeHistoryKind::Eew);
  scanGenerations(EarthquakeHistoryKind::Earthquake);
  Serial.printf("Earthquake history ready (EEW generations=%u, earthquake generations=%u).\n",
                static_cast<unsigned>(eewState_.count),
                static_cast<unsigned>(earthquakeState_.count));
}

bool EarthquakeHistoryService::enqueue(EarthquakeHistoryKind kind,
                                       const char* json, size_t length) {
  if (!storageAvailable_ || !json || length == 0 ||
      length >= MAX_RECORD_BYTES) {
    if (length >= MAX_RECORD_BYTES) {
      Serial.printf("Earthquake history record rejected: %u bytes exceeds limit.\n",
                    static_cast<unsigned>(length));
    }
    return false;
  }
  if (queueSize_ >= QUEUE_CAPACITY) {
    Serial.println("Earthquake history queue full; newest record dropped.");
    return false;
  }
  PendingRecord& pending = queue_[(queueHead_ + queueSize_) % QUEUE_CAPACITY];
  pending = PendingRecord();
  pending.kind = kind;
  pending.length = static_cast<uint16_t>(length);
  memcpy(pending.json, json, length);
  pending.json[length] = '\0';
  ++queueSize_;
  return true;
}

void EarthquakeHistoryService::loop() {
  if (!storageAvailable_ || queueSize_ == 0) return;
  PendingRecord& pending = queue_[queueHead_];
  if (static_cast<long>(millis() - pending.retryAt) < 0) return;
  if (appendRecord(pending)) {
    queueHead_ = (queueHead_ + 1) % QUEUE_CAPACITY;
    --queueSize_;
    return;
  }
  ++pending.attempts;
  if (pending.attempts >= WRITE_RETRY_LIMIT) {
    Serial.printf("Earthquake history record dropped after %u write attempts.\n",
                  static_cast<unsigned>(pending.attempts));
    queueHead_ = (queueHead_ + 1) % QUEUE_CAPACITY;
    --queueSize_;
  } else {
    pending.retryAt = millis() + RETRY_INTERVAL_MS;
  }
}

size_t EarthquakeHistoryService::generationCount(
    EarthquakeHistoryKind kind) const {
  return state(kind).count;
}

const EarthquakeHistoryService::GenerationInfo*
EarthquakeHistoryService::generationAt(EarthquakeHistoryKind kind,
                                       size_t index) const {
  const HistoryState& history = state(kind);
  return index < history.count ? &history.generations[index] : nullptr;
}

bool EarthquakeHistoryService::resolvePath(EarthquakeHistoryKind kind,
                                           uint32_t generation, char* path,
                                           size_t capacity) const {
  const HistoryState& history = state(kind);
  for (size_t index = 0; index < history.count; ++index) {
    if (history.generations[index].generation != generation) continue;
    makePath(kind, generation, path, capacity);
    return true;
  }
  return false;
}

const char* EarthquakeHistoryService::prefix(EarthquakeHistoryKind kind) {
  return kind == EarthquakeHistoryKind::Eew ? EEW_PREFIX : EARTHQUAKE_PREFIX;
}

const char* EarthquakeHistoryService::label(EarthquakeHistoryKind kind) {
  return kind == EarthquakeHistoryKind::Eew ? "EEW history"
                                             : "Earthquake history";
}

bool EarthquakeHistoryService::parseGeneration(const char* name,
                                               EarthquakeHistoryKind kind,
                                               uint32_t* generation) {
  if (!name || !generation) return false;
  if (name[0] == '/') ++name;
  const char* expectedPrefix = prefix(kind);
  const size_t prefixLength = strlen(expectedPrefix);
  if (strncmp(name, expectedPrefix, prefixLength) != 0) return false;
  const char* digits = name + prefixLength;
  if (strlen(digits) != 6 + strlen(SUFFIX) ||
      strcmp(digits + 6, SUFFIX) != 0) {
    return false;
  }
  uint32_t value = 0;
  for (size_t index = 0; index < 6; ++index) {
    if (digits[index] < '0' || digits[index] > '9') return false;
    value = value * 10 + static_cast<uint32_t>(digits[index] - '0');
  }
  if (value == 0) return false;
  *generation = value;
  return true;
}

void EarthquakeHistoryService::makePath(EarthquakeHistoryKind kind,
                                        uint32_t generation, char* path,
                                        size_t capacity) {
  snprintf(path, capacity, "/%s%06lu%s", prefix(kind),
           static_cast<unsigned long>(generation), SUFFIX);
}

void EarthquakeHistoryService::scanFile(const char* path,
                                        GenerationInfo* info) {
  if (!info) return;
  File file = SD.open(path, FILE_READ);
  if (!file) return;
  info->bytes = file.size();
  uint16_t lines = 0;
  int last = -1;
  uint8_t buffer[256];
  char currentLine[MAX_RECORD_BYTES] = {};
  char lastCompleteLine[MAX_RECORD_BYTES] = {};
  size_t currentLength = 0;
  bool currentOverflow = false;
  while (file.available()) {
    const size_t read = file.read(buffer, sizeof(buffer));
    for (size_t index = 0; index < read; ++index) {
      if (buffer[index] == '\n') {
        if (lines < UINT16_MAX) ++lines;
        if (!currentOverflow) {
          currentLine[currentLength] = '\0';
          strlcpy(lastCompleteLine, currentLine, sizeof(lastCompleteLine));
        } else {
          lastCompleteLine[0] = '\0';
        }
        currentLength = 0;
        currentOverflow = false;
      } else if (!currentOverflow) {
        if (currentLength + 1 < sizeof(currentLine)) {
          currentLine[currentLength++] = static_cast<char>(buffer[index]);
        } else {
          currentOverflow = true;
        }
      }
      last = buffer[index];
    }
  }
  file.close();
  info->records = lines;
  info->damagedTail = info->bytes > 0 && last != '\n';
  if (!info->damagedTail && lines > 0) {
    JsonDocument document;
    if (lastCompleteLine[0] == '\0' ||
        deserializeJson(document, lastCompleteLine)) {
      info->damagedTail = true;
      --info->records;
    }
  }
}

EarthquakeHistoryService::HistoryState& EarthquakeHistoryService::state(
    EarthquakeHistoryKind kind) {
  return kind == EarthquakeHistoryKind::Eew ? eewState_ : earthquakeState_;
}

const EarthquakeHistoryService::HistoryState& EarthquakeHistoryService::state(
    EarthquakeHistoryKind kind) const {
  return kind == EarthquakeHistoryKind::Eew ? eewState_ : earthquakeState_;
}

void EarthquakeHistoryService::scanGenerations(EarthquakeHistoryKind kind) {
  HistoryState& history = state(kind);
  history = HistoryState();
  File root = SD.open("/");
  if (!root) return;
  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      uint32_t generation = 0;
      if (parseGeneration(entry.name(), kind, &generation)) {
        GenerationInfo info;
        info.generation = generation;
        entry.close();
        char path[48];
        makePath(kind, generation, path, sizeof(path));
        scanFile(path, &info);
        if (history.count < MAX_GENERATIONS) {
          history.generations[history.count++] = info;
        } else {
          // Retain the newest generations if files from an older firmware or
          // interrupted cleanup left more than the configured maximum.
          size_t oldest = 0;
          for (size_t index = 1; index < history.count; ++index) {
            if (history.generations[index].generation <
                history.generations[oldest].generation) {
              oldest = index;
            }
          }
          if (generation > history.generations[oldest].generation) {
            history.generations[oldest] = info;
          }
        }
      } else {
        entry.close();
      }
    } else {
      entry.close();
    }
    entry = root.openNextFile();
  }
  root.close();
  for (size_t left = 0; left < history.count; ++left) {
    for (size_t right = left + 1; right < history.count; ++right) {
      if (!generationLess(history.generations[left],
                          history.generations[right])) {
        const GenerationInfo temporary = history.generations[left];
        history.generations[left] = history.generations[right];
        history.generations[right] = temporary;
      }
    }
  }
  // Remove stale generations that are older than the newest ten retained in
  // memory. This also repairs a previous interrupted cleanup on boot.
  root = SD.open("/");
  if (!root) return;
  entry = root.openNextFile();
  while (entry) {
    uint32_t generation = 0;
    const bool historyFile =
        !entry.isDirectory() && parseGeneration(entry.name(), kind, &generation);
    entry.close();
    if (historyFile) {
      bool retained = false;
      for (size_t index = 0; index < history.count; ++index) {
        if (history.generations[index].generation == generation) {
          retained = true;
          break;
        }
      }
      if (!retained) {
        char path[48];
        makePath(kind, generation, path, sizeof(path));
        if (SD.remove(path)) {
          Serial.printf("Excess earthquake history deleted: %s\n", path);
        } else {
          history.cleanupBlocked = true;
          Serial.printf("Unable to delete excess earthquake history: %s\n",
                        path);
        }
      }
    }
    entry = root.openNextFile();
  }
  root.close();
}

bool EarthquakeHistoryService::appendRecord(PendingRecord& record) {
  SdCardGuard guard(pdMS_TO_TICKS(50));
  if (!guard.locked()) {
    Serial.println("Earthquake history write deferred because microSD is busy.");
    return false;
  }
  HistoryState& history = state(record.kind);
  if (history.count == 0 ||
      history.generations[history.count - 1].records >=
          RECORDS_PER_GENERATION ||
      history.generations[history.count - 1].damagedTail) {
    if (!createNextGeneration(record.kind)) return false;
  }
  GenerationInfo& current = history.generations[history.count - 1];
  char path[48];
  makePath(record.kind, current.generation, path, sizeof(path));
  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("Unable to open earthquake history file: %s\n", path);
    return false;
  }
  const size_t jsonWritten = file.write(
      reinterpret_cast<const uint8_t*>(record.json), record.length);
  const size_t newlineWritten = file.write(static_cast<uint8_t>('\n'));
  file.close();
  if (jsonWritten != record.length || newlineWritten != 1) {
    current.damagedTail = true;
    File damaged = SD.open(path, FILE_READ);
    if (damaged) {
      current.bytes = damaged.size();
      damaged.close();
    }
    Serial.printf("Earthquake history write incomplete: %s\n", path);
    return false;
  }
  ++current.records;
  current.bytes += record.length + 1;
  Serial.printf("Earthquake history appended: %s (%u/%u).\n", path,
                static_cast<unsigned>(current.records),
                static_cast<unsigned>(RECORDS_PER_GENERATION));
  return true;
}

bool EarthquakeHistoryService::createNextGeneration(
    EarthquakeHistoryKind kind) {
  HistoryState& history = state(kind);
  if (history.cleanupBlocked) {
    scanGenerations(kind);
    if (history.cleanupBlocked) return false;
  }
  const uint32_t next = history.count == 0
                            ? 1
                            : history.generations[history.count - 1].generation +
                                  1;
  if (next > 999999) {
    Serial.println("Earthquake history generation limit reached.");
    return false;
  }
  if (history.count >= MAX_GENERATIONS) {
    char oldestPath[48];
    makePath(kind, history.generations[0].generation, oldestPath,
             sizeof(oldestPath));
    if (!SD.remove(oldestPath)) {
      Serial.printf("Unable to delete oldest earthquake history: %s\n",
                    oldestPath);
      return false;
    }
    for (size_t index = 1; index < history.count; ++index) {
      history.generations[index - 1] = history.generations[index];
    }
    --history.count;
    Serial.printf("Oldest earthquake history deleted: %s\n", oldestPath);
  }
  char path[48];
  makePath(kind, next, path, sizeof(path));
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Unable to create earthquake history generation: %s\n",
                  path);
    return false;
  }
  file.close();
  GenerationInfo info;
  info.generation = next;
  history.generations[history.count++] = info;
  Serial.printf("Earthquake history generation created: %s\n", path);
  return true;
}

void EarthquakeHistoryService::appendEscapedHtml(String& html,
                                                 const char* text) {
  if (!text) return;
  while (*text) {
    switch (*text) {
      case '&': html += F("&amp;"); break;
      case '<': html += F("&lt;"); break;
      case '>': html += F("&gt;"); break;
      case '"': html += F("&quot;"); break;
      case '\'': html += F("&#39;"); break;
      default: html += *text; break;
    }
    ++text;
  }
}

bool EarthquakeHistoryService::appendRecordRow(String& html,
                                               EarthquakeHistoryKind kind,
                                               const char* json) {
  JsonDocument document;
  if (deserializeJson(document, json)) return false;
  html += F("<tr><td>");
  html += kind == EarthquakeHistoryKind::Eew ? F("EEW") : F("Earthquake");
  if (document["test"] | false) html += F(" [TEST]");
  if (document["cancelled"] | false) html += F(" [CANCELLED]");
  const char* correction = document["correction"] | "None";
  if (strcmp(correction, "None") != 0) html += F(" [CORRECTED]");
  html += F("</td><td>");
  appendEscapedHtml(html, document["issue_time"] |
                              document["event_time"] | "-");
  html += F("</td><td>");
  appendEscapedHtml(html, document["hypocenter"] | "-");
  html += F("</td><td>");
  html += String(document["target_max_scale"] |
                 document["max_scale"] | -1);
  html += F("</td></tr>");
  return true;
}

size_t EarthquakeHistoryService::appendLatestRowsFromFileLocked(
    String& html, EarthquakeHistoryKind kind, const char* path,
    size_t maximumRows) const {
  File file = SD.open(path, FILE_READ);
  if (!file || maximumRows == 0) return 0;
  const size_t size = file.size();
  if (size == 0) {
    file.close();
    return 0;
  }
  char reversed[MAX_RECORD_BYTES] = {};
  size_t length = 0;
  size_t rows = 0;
  size_t position = size;
  if (!file.seek(size - 1)) {
    file.close();
    return 0;
  }
  const bool endsWithNewline = file.read() == '\n';
  bool discardIncompleteTail = !endsWithNewline;
  uint8_t buffer[256];
  while (position > 0 && rows < maximumRows) {
    const size_t chunk = min(position, sizeof(buffer));
    position -= chunk;
    if (!file.seek(position) || file.read(buffer, chunk) != chunk) break;
    for (size_t offset = chunk; offset > 0 && rows < maximumRows; --offset) {
      const char value = static_cast<char>(buffer[offset - 1]);
      if (value == '\n') {
        if (discardIncompleteTail) {
          length = 0;
          discardIncompleteTail = false;
          continue;
        }
        if (length == 0) continue;
        char line[MAX_RECORD_BYTES] = {};
        for (size_t index = 0; index < length; ++index) {
          line[index] = reversed[length - index - 1];
        }
        if (appendRecordRow(html, kind, line)) ++rows;
        length = 0;
      } else if (length + 1 < sizeof(reversed)) {
        reversed[length++] = value;
      } else {
        length = 0;
      }
    }
  }
  if (!discardIncompleteTail && rows < maximumRows && length > 0 &&
      position == 0) {
    char line[MAX_RECORD_BYTES] = {};
    for (size_t index = 0; index < length; ++index) {
      line[index] = reversed[length - index - 1];
    }
    if (appendRecordRow(html, kind, line)) ++rows;
  }
  file.close();
  return rows;
}

void EarthquakeHistoryService::appendLatestRowsLocked(
    String& html, EarthquakeHistoryKind kind, size_t maximumRows) const {
  const HistoryState& history = state(kind);
  size_t rows = 0;
  for (size_t reverse = history.count; reverse > 0 && rows < maximumRows;
       --reverse) {
    char path[48];
    makePath(kind, history.generations[reverse - 1].generation, path,
             sizeof(path));
    rows += appendLatestRowsFromFileLocked(html, kind, path,
                                           maximumRows - rows);
  }
}

void EarthquakeHistoryService::appendKindWebSectionLocked(
    String& html, EarthquakeHistoryKind kind) const {
  const HistoryState& history = state(kind);
  html += F("<h2>");
  html += label(kind);
  html += F("</h2><p>Generations: ");
  html += String(history.count);
  html += F(" / ");
  html += String(MAX_GENERATIONS);
  html += F("</p>");
  if (history.count == 0) {
    html += F("<p>Not created yet</p>");
    return;
  }
  if (history.count == MAX_GENERATIONS) {
    html += F("<p>Next deletion: ");
    char path[48];
    makePath(kind, history.generations[0].generation, path, sizeof(path));
    appendEscapedHtml(html, path);
    html += F("</p>");
  }
  html += F("<ul>");
  for (size_t index = history.count; index > 0; --index) {
    const GenerationInfo& info = history.generations[index - 1];
    html += F("<li>Generation ");
    html += String(info.generation);
    html += F(": ");
    html += String(info.records);
    html += F(" records, ");
    html += String(static_cast<unsigned long>(info.bytes));
    html += F(" bytes");
    if (info.damagedTail) html += F(" (incomplete final line)");
    html += F(" - <a href=\"/download/earthquake-history?type=");
    html += kind == EarthquakeHistoryKind::Eew ? F("eew") : F("earthquake");
    html += F("&amp;generation=");
    char generation[7];
    snprintf(generation, sizeof(generation), "%06lu",
             static_cast<unsigned long>(info.generation));
    html += generation;
    html += F("\">Download</a></li>");
  }
  html += F("</ul><h3>Latest records</h3><table border=\"1\"><tr><th>Type</th><th>Time</th><th>Hypocenter</th><th>Scale code</th></tr>");
  appendLatestRowsLocked(html, kind, 5);
  html += F("</table>");
}

void EarthquakeHistoryService::appendWebSectionLocked(String& html) const {
  appendKindWebSectionLocked(html, EarthquakeHistoryKind::Eew);
  appendKindWebSectionLocked(html, EarthquakeHistoryKind::Earthquake);
}
