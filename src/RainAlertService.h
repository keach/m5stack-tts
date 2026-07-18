#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "SpeechService.h"

class RainAlertService {
 public:
  void begin();
  void evaluate(bool rainingNow, const char* condition, float rainLastHour,
                bool audioAllowed, SpeechService& speech);
  bool isRainActive() const;

 private:
  bool appendLog(const char* condition, float rainLastHour, bool audioPlayed,
                 time_t alertTime);
  void saveState();

  static constexpr uint8_t DRY_OBSERVATIONS_TO_REARM = 2;
  Preferences preferences_;
  bool rainActive_ = false;
  uint8_t dryObservations_ = 0;
};
