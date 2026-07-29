#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "SpeechService.h"

class RainForecastAlertService {
 public:
  void begin();
  bool evaluate(bool forecastMatches, bool rainingNow, time_t forecastAt,
                uint8_t probabilityPercent, float rainThreeHours,
                bool audioAllowed, SpeechService& speech);
  bool isActive() const;
  uint8_t probabilityPercent() const;
  float rainThreeHours() const;

 private:
  bool appendLog(time_t forecastAt, uint8_t probabilityPercent,
                 float rainThreeHours, bool audioPlayed, time_t alertTime);
  void saveState();

  static constexpr uint8_t CLEAR_OBSERVATIONS_TO_REARM = 2;
  Preferences preferences_;
  bool active_ = false;
  uint8_t clearObservations_ = 0;
  time_t forecastAt_ = 0;
  uint8_t probabilityPercent_ = 0;
  float rainThreeHours_ = 0;
};
