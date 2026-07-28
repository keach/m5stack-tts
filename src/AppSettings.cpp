#include "AppSettings.h"

namespace {
constexpr char NVS_NAMESPACE[] = "app_settings";
constexpr char CLOCK_SECONDS_KEY[] = "clock_secs";
constexpr char VOLUME_KEY[] = "volume";
constexpr char SCHEDULE_ENABLED_KEYS[][8] = {"fc_en0", "fc_en1", "fc_en2"};
constexpr char SCHEDULE_MINUTE_KEYS[][8] = {"fc_min0", "fc_min1", "fc_min2"};
constexpr char SCHEDULE_LAST_RUN_KEYS[][8] = {"fc_run0", "fc_run1", "fc_run2"};
}  // namespace

void AppSettings::begin() {
  preferences_.begin(NVS_NAMESPACE, false);
  clockPrecision_ = preferences_.getBool(CLOCK_SECONDS_KEY, false)
                        ? ClockDisplayPrecision::Seconds
                        : ClockDisplayPrecision::Minutes;
  volumePercent_ = min(preferences_.getUChar(VOLUME_KEY,
                                             DEFAULT_VOLUME_PERCENT),
                       static_cast<uint8_t>(100));
  for (size_t index = 0; index < FORECAST_SCHEDULE_COUNT; ++index) {
    ForecastSchedule& schedule = forecastSchedules_[index];
    schedule.enabled = preferences_.getBool(SCHEDULE_ENABLED_KEYS[index], false);
    schedule.minuteOfDay =
        min(preferences_.getUShort(SCHEDULE_MINUTE_KEYS[index], 0),
            static_cast<uint16_t>(23 * 60 + 45));
    schedule.minuteOfDay -= schedule.minuteOfDay % 15;
    schedule.lastRunDate =
        preferences_.getUInt(SCHEDULE_LAST_RUN_KEYS[index], 0);
  }
}

void AppSettings::save(ClockDisplayPrecision clockPrecision,
                       uint8_t volumePercent,
                       const ForecastSchedule* forecastSchedules) {
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
  if (!forecastSchedules) {
    return;
  }
  for (size_t index = 0; index < FORECAST_SCHEDULE_COUNT; ++index) {
    const ForecastSchedule& source = forecastSchedules[index];
    ForecastSchedule& target = forecastSchedules_[index];
    const uint16_t minuteOfDay =
        min(source.minuteOfDay, static_cast<uint16_t>(23 * 60 + 45));
    if (target.enabled != source.enabled) {
      preferences_.putBool(SCHEDULE_ENABLED_KEYS[index], source.enabled);
      target.enabled = source.enabled;
    }
    if (target.minuteOfDay != minuteOfDay) {
      preferences_.putUShort(SCHEDULE_MINUTE_KEYS[index], minuteOfDay);
      target.minuteOfDay = minuteOfDay;
    }
  }
}

void AppSettings::markForecastScheduleRun(size_t index, uint32_t date) {
  if (index >= FORECAST_SCHEDULE_COUNT ||
      forecastSchedules_[index].lastRunDate == date) {
    return;
  }
  forecastSchedules_[index].lastRunDate = date;
  preferences_.putUInt(SCHEDULE_LAST_RUN_KEYS[index], date);
}

ClockDisplayPrecision AppSettings::clockPrecision() const {
  return clockPrecision_;
}

uint8_t AppSettings::volumePercent() const { return volumePercent_; }

const AppSettings::ForecastSchedule& AppSettings::forecastSchedule(
    size_t index) const {
  return forecastSchedules_[min(index, FORECAST_SCHEDULE_COUNT - 1)];
}
