#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "EarthquakeService.h"

class SpeechService;

class EarthquakeSpeechService {
 public:
  static constexpr size_t MAX_QUEUE_SIZE = 4;

  void begin(bool eewEnabled, bool earthquakeEnabled);
  void setEnabled(bool eewEnabled, bool earthquakeEnabled);
  void enqueue(const SeismicEvent& event);
  void loop(SpeechService& speech);
  bool activeOrPending() const;
  void clearPending();

 private:
  static constexpr size_t MAX_EEW_STATE_COUNT = MAX_QUEUE_SIZE;

  enum class RequestKind : uint8_t {
    Eew,
    Earthquake,
  };

  struct Request {
    RequestKind kind = RequestKind::Earthquake;
    uint8_t priority = 0;
    bool eewInitialPending = false;
    bool eewCancelled = false;
    char eventKey[128] = {};
    char signature[192] = {};
    char message[448] = {};
  };

  struct EewState {
    char eventId[40] = {};
    char signature[192] = {};
  };

  bool eewEnabled_ = true;
  bool earthquakeEnabled_ = true;
  bool active_ = false;
  Preferences preferences_;
  Request queue_[MAX_QUEUE_SIZE];
  size_t queueCount_ = 0;
  EewState eewStates_[MAX_EEW_STATE_COUNT];
  size_t eewStateCount_ = 0;
  char lastEarthquakeKey_[128] = {};
  char lastEarthquakeSignature_[192] = {};

  bool isEnabled(RequestKind kind) const;
  bool buildRequest(const SeismicEvent& event, Request* request) const;
  bool buildEewRequest(const SeismicEvent& event, Request* request) const;
  bool buildEarthquakeRequest(const SeismicEvent& event,
                              Request* request) const;
  void enqueueRequest(const Request& request);
  void removeAt(size_t index);
  size_t highestPriorityIndex() const;
  void rememberStarted(const Request& request);
  bool hasEewStarted(const char* eventId) const;
  size_t findEewState(const char* eventId) const;
  void rememberEewStarted(const Request& request);
  static const char* scaleText(int scale);
  static void appendTestPrefix(const SeismicEvent& event, char* message,
                               size_t capacity);
};
