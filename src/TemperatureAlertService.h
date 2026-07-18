#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "SpeechService.h"

class TemperatureAlertService {
 public:
  void begin();
  bool evaluate(float temperature, bool audioAllowed, SpeechService& speech);
  int activeThreshold(float temperature) const;

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

  static constexpr int ALERT_COUNT = 2;
  static constexpr time_t REARM_DELAY_SECONDS = 3 * 60 * 60;
  AlertState alerts_[ALERT_COUNT] = {
      {30, "armed30", "last30", true, 0},
      {35, "armed35", "last35", true, 0},
  };
  Preferences preferences_;
};
