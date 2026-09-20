#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include "P2PConnectionStatus.h"

class EarthquakeHistoryService;

enum class SeismicEventType : uint8_t {
  None,
  Earthquake,
  Eew,
};

enum class SeismicSoundType : uint8_t {
  None,
  Short,
  EewWarning,
};

struct SeismicEvent {
  SeismicEventType type = SeismicEventType::None;
  char id[48] = {};
  char eventId[40] = {};
  char hypocenter[64] = {};
  char targetAreas[80] = {};
  char matchedAreas[192] = {};
  char eventTime[20] = {};
  int serial = 0;
  int maxScale = -1;
  int nationalMaxScale = -1;
  float magnitude = -1.0F;
  bool cancelled = false;
  bool corrected = false;
  bool test = false;
  bool targetMatched = false;
  char logicalKey[128] = {};
  unsigned long receivedAt = 0;
};

class EarthquakeService {
 public:
  using ConnectionState = P2PConnectionState;
  ConnectionState connectionState() const;
  bool usesSandbox() const { return useSandbox_; }
  static const char* connectionStateText(ConnectionState state);
  void begin(const char* const* targetPrefectures, size_t targetCount,
             bool useSandbox, bool allowSandboxAudio,
             EarthquakeHistoryService* historyService = nullptr);
  void loop();
  bool connectionAttemptDue() const;
  bool pauseForNetworkRequest();
  void resumeAfterNetworkRequest();
  bool active() const;
  const SeismicEvent& current() const;
  bool consumeDisplayChanged();
  SeismicSoundType consumeSoundRequested();
  bool consumeWakeRequested();
  bool consumeSpeechEvent(SeismicEvent* event);

 private:
  static constexpr size_t MAX_TARGET_PREFECTURES = 8;
  static constexpr size_t RECENT_ID_COUNT = 8;
  static constexpr size_t MAX_SPEECH_EVENT_COUNT = 4;

  static void eventThunk(WStype_t type, uint8_t* payload, size_t length);
  void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
  void processMessage(const uint8_t* payload, size_t length);
  void processEew(JsonDocument& document);
  void processEarthquake(JsonDocument& document);
  void enqueueEewHistory(JsonDocument& document, const SeismicEvent& event);
  void enqueueEarthquakeHistory(JsonDocument& document,
                                const SeismicEvent& event,
                                int nationalMaxScale,
                                const char* logicalKey);
  void enqueueSpeechEvent(const SeismicEvent& event);
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
  void setConnectionState(ConnectionState state);

  WebSocketsClient webSocket_;
  Preferences preferences_;
  EarthquakeHistoryService* historyService_ = nullptr;
  const char* targets_[MAX_TARGET_PREFECTURES] = {};
  size_t targetCount_ = 0;
  char recentIds_[RECENT_ID_COUNT][48] = {};
  size_t recentIdCount_ = 0;
  size_t nextRecentId_ = 0;
  SeismicEvent eew_;
  SeismicEvent earthquake_;
  SeismicEvent empty_;
  bool useSandbox_ = false;
  bool allowSandboxAudio_ = false;
  bool connected_ = false;
  bool connecting_ = false;
  bool paused_ = false;
  bool started_ = false;
  ConnectionState connectionState_ = ConnectionState::NotStarted;
  bool displayChanged_ = false;
  SeismicSoundType soundRequested_ = SeismicSoundType::None;
  bool wakeRequested_ = false;
  uint8_t reconnectStep_ = 0;
  unsigned long reconnectAt_ = 0;
  unsigned long connectStartedAt_ = 0;
  SeismicEventType selectedType_ = SeismicEventType::None;
  char lastEewEventId_[40] = {};
  char lastWarnedEewEventId_[40] = {};
  char lastShortEewEventId_[40] = {};
  char lastSoundedEarthquakeKey_[128] = {};
  int lastEewSerial_ = 0;
  SeismicEvent speechEvents_[MAX_SPEECH_EVENT_COUNT];
  size_t speechEventCount_ = 0;

  static EarthquakeService* instance_;
};
