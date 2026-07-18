#include "AppSettings.h"

namespace {
constexpr char NVS_NAMESPACE[] = "app_settings";
constexpr char CLOCK_SECONDS_KEY[] = "clock_secs";
constexpr char VOLUME_KEY[] = "volume";
}  // namespace

void AppSettings::begin() {
  preferences_.begin(NVS_NAMESPACE, false);
  clockPrecision_ = preferences_.getBool(CLOCK_SECONDS_KEY, false)
                        ? ClockDisplayPrecision::Seconds
                        : ClockDisplayPrecision::Minutes;
  volumePercent_ = min(preferences_.getUChar(VOLUME_KEY,
                                             DEFAULT_VOLUME_PERCENT),
                       static_cast<uint8_t>(100));
}

void AppSettings::save(ClockDisplayPrecision clockPrecision,
                       uint8_t volumePercent) {
  const uint8_t constrainedVolume =
      min(volumePercent, static_cast<uint8_t>(100));
  if (clockPrecision_ != clockPrecision) {
    preferences_.putBool(CLOCK_SECONDS_KEY,
                         clockPrecision == ClockDisplayPrecision::Seconds);
    clockPrecision_ = clockPrecision;
  }
  if (volumePercent_ != constrainedVolume) {
    preferences_.putUChar(VOLUME_KEY, constrainedVolume);
    volumePercent_ = constrainedVolume;
  }
}

ClockDisplayPrecision AppSettings::clockPrecision() const {
  return clockPrecision_;
}

uint8_t AppSettings::volumePercent() const { return volumePercent_; }
