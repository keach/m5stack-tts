#pragma once

#include <Arduino.h>
#include <Preferences.h>

enum class ClockDisplayPrecision : uint8_t {
  Minutes = 0,
  Seconds = 1,
};

class AppSettings {
 public:
  static constexpr uint8_t DEFAULT_VOLUME_PERCENT = 50;
  static constexpr size_t FORECAST_SCHEDULE_COUNT = 3;

  struct ForecastSchedule {
    bool enabled = false;
    uint16_t minuteOfDay = 0;
    uint32_t lastRunDate = 0;
  };

  void begin();
  void save(ClockDisplayPrecision clockPrecision, uint8_t volumePercent,
            const ForecastSchedule* forecastSchedules);
  void markForecastScheduleRun(size_t index, uint32_t date);

  ClockDisplayPrecision clockPrecision() const;
  uint8_t volumePercent() const;
  const ForecastSchedule& forecastSchedule(size_t index) const;

 private:
  Preferences preferences_;
  ClockDisplayPrecision clockPrecision_ = ClockDisplayPrecision::Minutes;
  uint8_t volumePercent_ = DEFAULT_VOLUME_PERCENT;
  ForecastSchedule forecastSchedules_[FORECAST_SCHEDULE_COUNT];
};
