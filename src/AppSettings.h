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
  static constexpr bool DEFAULT_DISPLAY_SLEEP_ENABLED = true;
  static constexpr uint8_t DEFAULT_DISPLAY_SLEEP_MINUTES = 5;
  static constexpr uint8_t DEFAULT_DISPLAY_BRIGHTNESS_PERCENT = 40;
  static constexpr size_t FORECAST_SCHEDULE_COUNT = 3;

  struct ForecastSchedule {
    bool enabled = false;
    uint16_t minuteOfDay = 0;
    uint32_t lastRunDate = 0;
  };

  void begin();
  void save(ClockDisplayPrecision clockPrecision, uint8_t volumePercent,
            bool displaySleepEnabled, uint8_t displaySleepMinutes,
            uint8_t displayBrightnessPercent,
            const ForecastSchedule* forecastSchedules);
  void markForecastScheduleRun(size_t index, uint32_t date);

  ClockDisplayPrecision clockPrecision() const;
  uint8_t volumePercent() const;
  bool displaySleepEnabled() const;
  uint8_t displaySleepMinutes() const;
  uint8_t displayBrightnessPercent() const;
  static uint8_t displayBrightnessLevel(uint8_t percent);
  const ForecastSchedule& forecastSchedule(size_t index) const;

 private:
  Preferences preferences_;
  ClockDisplayPrecision clockPrecision_ = ClockDisplayPrecision::Minutes;
  uint8_t volumePercent_ = DEFAULT_VOLUME_PERCENT;
  bool displaySleepEnabled_ = DEFAULT_DISPLAY_SLEEP_ENABLED;
  uint8_t displaySleepMinutes_ = DEFAULT_DISPLAY_SLEEP_MINUTES;
  uint8_t displayBrightnessPercent_ = DEFAULT_DISPLAY_BRIGHTNESS_PERCENT;
  ForecastSchedule forecastSchedules_[FORECAST_SCHEDULE_COUNT];
};
