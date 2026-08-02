#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "SpeechService.h"

class RainAlertService {
 public:
  void begin();
  bool evaluate(bool rainingNow, const char* condition, float rainLastHour,
                bool audioAllowed, SpeechService& speech);
  bool isRainActive() const;
  void processPendingLog();

 private:
  bool appendLog(const char* condition, float rainLastHour, bool audioPlayed,
                 time_t alertTime);
  void saveState();
  void scheduleLogRetry(const char* condition, float rainLastHour,
                        bool audioPlayed, time_t alertTime);
  struct PendingLog {
    char condition[32] = "";
    float rainLastHour = 0;
    bool audioPlayed = false;
    time_t alertTime = 0;
    unsigned long nextRetryAt = 0;
    uint8_t retryCount = 0;
    bool active = false;
  };

  static constexpr uint8_t DRY_OBSERVATIONS_TO_REARM = 2;
  Preferences preferences_;
  bool rainActive_ = false;
  uint8_t dryObservations_ = 0;
  PendingLog pendingLog_;
};
