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

  void begin();
  void save(ClockDisplayPrecision clockPrecision, uint8_t volumePercent);

  ClockDisplayPrecision clockPrecision() const;
  uint8_t volumePercent() const;

 private:
  Preferences preferences_;
  ClockDisplayPrecision clockPrecision_ = ClockDisplayPrecision::Minutes;
  uint8_t volumePercent_ = DEFAULT_VOLUME_PERCENT;
};
