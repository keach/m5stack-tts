#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebSocketsClient.h>

enum class SeismicEventType : uint8_t {
  None,
  Earthquake,
  Eew,
};

struct SeismicEvent {
  SeismicEventType type = SeismicEventType::None;
  char id[48] = {};
  char eventId[40] = {};
  char hypocenter[64] = {};
  char targetAreas[80] = {};
  char eventTime[20] = {};
  int serial = 0;
  int maxScale = -1;
  float magnitude = -1.0F;
  bool cancelled = false;
  bool test = false;
  bool targetMatched = false;
  unsigned long receivedAt = 0;
};

class EarthquakeService {
 public:
  void begin(const char* const* targetPrefectures, size_t targetCount,
             bool useSandbox);
  void loop();
  bool active() const;
  const SeismicEvent& current() const;
  bool consumeDisplayChanged();
  bool consumeWarningRequested();
  bool consumeWakeRequested();

 private:
  static constexpr size_t MAX_TARGET_PREFECTURES = 8;
  static constexpr size_t RECENT_ID_COUNT = 8;

  static void eventThunk(WStype_t type, uint8_t* payload, size_t length);
  void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
  void processMessage(const uint8_t* payload, size_t length);
  void processEew(JsonDocument& document);
  void processEarthquake(JsonDocument& document);
  bool isDuplicateId(const char* id) const;
  void rememberId(const char* id);
  bool isTargetPrefecture(const char* prefecture) const;
  bool eewAreaMatchesPrefecture(const char* area,
                                const char* prefecture) const;
  bool isStale(const char* basicTime) const;
  void selectCurrentEvent();
  void expireEvents();
  void scheduleReconnect();
  void connect();

  WebSocketsClient webSocket_;
  Preferences preferences_;
  const char* targets_[MAX_TARGET_PREFECTURES] = {};
  size_t targetCount_ = 0;
  char recentIds_[RECENT_ID_COUNT][48] = {};
  size_t recentIdCount_ = 0;
  size_t nextRecentId_ = 0;
  SeismicEvent eew_;
  SeismicEvent earthquake_;
  SeismicEvent empty_;
  bool useSandbox_ = false;
  bool connected_ = false;
  bool connecting_ = false;
  bool started_ = false;
  bool displayChanged_ = false;
  bool warningRequested_ = false;
  bool wakeRequested_ = false;
  uint8_t reconnectStep_ = 0;
  unsigned long reconnectAt_ = 0;
  SeismicEventType selectedType_ = SeismicEventType::None;
  char lastEewEventId_[40] = {};
  char lastWarnedEewEventId_[40] = {};
  int lastEewSerial_ = 0;

  static EarthquakeService* instance_;
};
