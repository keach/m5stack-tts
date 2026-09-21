#pragma once

#include <Arduino.h>

class WeatherWarningService {
 public:
  static constexpr size_t MAX_TARGETS = 8;
  static constexpr size_t MAX_WARNINGS = 12;

  enum class Status {
    NotAttempted,
    Available,
    NotConfigured,
    WiFiUnavailable,
    FeedFailed,
    XmlFailed,
    ParseFailed,
  };

  struct Warning {
    char area[24] = {};
    char name[40] = {};
    bool special = false;
    bool continued = false;
  };

  void begin(const char* const* targetCodes, const char* const* targetNames,
             size_t targetCount);
  bool poll();
  bool hasWarnings() const { return warningCount_ > 0; }
  size_t count() const { return warningCount_; }
  const Warning* warning(size_t index) const;
  Status status() const { return status_; }
  const char* statusText() const;
  const Warning* highestPriority() const;

 private:
  struct Target {
    char code[8] = {};
    char name[24] = {};
  };

  static constexpr size_t MAX_SEEN_IDS = 16;
  Target targets_[MAX_TARGETS];
  size_t targetCount_ = 0;
  Warning warnings_[MAX_WARNINGS];
  size_t warningCount_ = 0;
  char seenIds_[MAX_SEEN_IDS][96] = {};
  size_t seenCount_ = 0;
  Status status_ = Status::NotAttempted;

  bool isSeen(const char* id) const;
  void rememberId(const char* id);
  bool parseEntry(const String& entry, String* id, String* url) const;
  bool applyWarningXml(const String& url, bool* fetched);
  bool applyWarningItem(const String& item);
  int targetIndexForCode(const char* code) const;
  int targetIndexForUrl(const String& url) const;
  void clearTargetWarnings(size_t targetIndex);
  void addWarning(const Target& target, const char* name, bool special,
                  bool continued);
};
