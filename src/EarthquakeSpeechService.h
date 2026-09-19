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

 private:
  enum class RequestKind : uint8_t {
    Eew,
    Earthquake,
  };

  struct Request {
    RequestKind kind = RequestKind::Earthquake;
    uint8_t priority = 0;
    SeismicEvent event;
    char signature[192] = {};
    char message[512] = {};
  };

  bool eewEnabled_ = true;
  bool earthquakeEnabled_ = true;
  bool active_ = false;
  Preferences preferences_;
  Request queue_[MAX_QUEUE_SIZE];
  size_t queueCount_ = 0;
  char lastEewEventId_[40] = {};
  char lastEewSignature_[192] = {};
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
  static const char* scaleText(int scale);
  static void appendTestPrefix(const SeismicEvent& event, char* message,
                               size_t capacity);
};
