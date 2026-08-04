#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "SpeechService.h"

class TemperatureAlertService {
 public:
  void begin();
  bool evaluate(float temperature, bool audioAllowed,
                bool* alertTriggered = nullptr,
                int* triggeredThreshold = nullptr);
  void notify(float temperature, int threshold, SpeechService& speech);
  int activeThreshold(float temperature) const;
  void processPendingLogs();

 private:
  struct AlertState {
    int threshold;
    const char* armedKey;
    const char* lastAlertKey;
    bool armed;
    time_t lastAlertAt;
  };

  bool appendLog(float temperature, int threshold, bool audioPlayed,
                 time_t alertTime);
  void saveState(const AlertState& state);
  void scheduleLogRetry(float temperature, int threshold, bool audioPlayed,
                        time_t alertTime);
  struct PendingLog {
    float temperature = 0;
    int threshold = 0;
    bool audioPlayed = false;
    time_t alertTime = 0;
    unsigned long nextRetryAt = 0;
    uint8_t retryCount = 0;
    bool active = false;
  };

  static constexpr int ALERT_COUNT = 3;
  static constexpr time_t REARM_DELAY_SECONDS = 3 * 60 * 60;
  AlertState alerts_[ALERT_COUNT] = {
      {30, "armed30", "last30", true, 0},
      {35, "armed35", "last35", true, 0},
      {40, "armed40", "last40", true, 0},
  };
  Preferences preferences_;
  PendingLog pendingLogs_[ALERT_COUNT];
};
