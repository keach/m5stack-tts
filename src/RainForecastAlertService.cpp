#include "RainForecastAlertService.h"

#include <SD.h>
#include <time.h>

#include "SpeechNumberFormatter.h"

namespace {
constexpr char NVS_NAMESPACE[] = "rain_fc_alert";
constexpr char ACTIVE_KEY[] = "active";
constexpr char CLEAR_COUNT_KEY[] = "clear_count";
constexpr char FORECAST_AT_KEY[] = "forecast_at";
constexpr char PROBABILITY_KEY[] = "probability";
constexpr char RAIN_KEY[] = "rain_3h";
constexpr char LOG_PATH[] = "/rain_forecast_alerts.csv";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
}  // namespace

void RainForecastAlertService::begin() {
  preferences_.begin(NVS_NAMESPACE, false);
  active_ = preferences_.getBool(ACTIVE_KEY, false);
  clearObservations_ =
      min(preferences_.getUChar(CLEAR_COUNT_KEY, 0),
          CLEAR_OBSERVATIONS_TO_REARM);
  forecastAt_ =
      static_cast<time_t>(preferences_.getULong(FORECAST_AT_KEY, 0));
  probabilityPercent_ = preferences_.getUChar(PROBABILITY_KEY, 0);
  rainThreeHours_ = preferences_.getFloat(RAIN_KEY, 0.0F);
}

void RainForecastAlertService::saveState() {
  preferences_.putBool(ACTIVE_KEY, active_);
  preferences_.putUChar(CLEAR_COUNT_KEY, clearObservations_);
  preferences_.putULong(FORECAST_AT_KEY,
                        static_cast<uint32_t>(forecastAt_));
  preferences_.putUChar(PROBABILITY_KEY, probabilityPercent_);
  preferences_.putFloat(RAIN_KEY, rainThreeHours_);
}

bool RainForecastAlertService::appendLog(time_t forecastAt,
                                         uint8_t probabilityPercent,
                                         float rainThreeHours,
                                         bool audioPlayed,
                                         time_t alertTime) {
  const bool needsHeader = !SD.exists(LOG_PATH);
  File file = SD.open(LOG_PATH, FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open %s.\n", LOG_PATH);
    return false;
  }
  if (needsHeader) {
    file.println(
        "datetime,forecast_at,probability_pct,rain_3h_mm,audio_played");
  }

  char formattedAlertTime[20] = "unknown";
  char formattedForecastTime[20] = "unknown";
  if (alertTime >= MINIMUM_VALID_TIME) {
    tm timeInfo = {};
    localtime_r(&alertTime, &timeInfo);
    strftime(formattedAlertTime, sizeof(formattedAlertTime),
             "%Y-%m-%d %H:%M:%S", &timeInfo);
  }
  if (forecastAt >= MINIMUM_VALID_TIME) {
    tm timeInfo = {};
    localtime_r(&forecastAt, &timeInfo);
    strftime(formattedForecastTime, sizeof(formattedForecastTime),
             "%Y-%m-%d %H:%M:%S", &timeInfo);
  }

  const size_t written =
      file.printf("%s,%s,%u,%.1f,%s\n", formattedAlertTime,
                  formattedForecastTime, probabilityPercent, rainThreeHours,
                  audioPlayed ? "true" : "false");
  file.close();
  return written > 0;
}

bool RainForecastAlertService::evaluate(bool forecastMatches, bool rainingNow,
                                        time_t forecastAt,
                                        uint8_t probabilityPercent,
                                        float rainThreeHours,
                                        bool audioAllowed,
                                        SpeechService& speech) {
  if (!forecastMatches) {
    if (!active_) {
      return false;
    }
    if (clearObservations_ < CLEAR_OBSERVATIONS_TO_REARM) {
      ++clearObservations_;
      saveState();
    }
    if (clearObservations_ >= CLEAR_OBSERVATIONS_TO_REARM) {
      active_ = false;
      clearObservations_ = 0;
      forecastAt_ = 0;
      probabilityPercent_ = 0;
      rainThreeHours_ = 0;
      saveState();
      Serial.println(
          "Rain forecast alert rearmed after two clear forecasts.");
    }
    return false;
  }

  if (active_) {
    const bool changed =
        clearObservations_ != 0 || forecastAt_ != forecastAt ||
        probabilityPercent_ != probabilityPercent ||
        rainThreeHours_ != rainThreeHours;
    clearObservations_ = 0;
    forecastAt_ = forecastAt;
    probabilityPercent_ = probabilityPercent;
    rainThreeHours_ = rainThreeHours;
    if (changed) {
      saveState();
    }
    return false;
  }

  if (rainingNow) {
    Serial.println(
        "Rain forecast alert suppressed because rain is already active.");
    return false;
  }

  active_ = true;
  clearObservations_ = 0;
  forecastAt_ = forecastAt;
  probabilityPercent_ = probabilityPercent;
  rainThreeHours_ = rainThreeHours;
  saveState();

  const time_t now = time(nullptr);
  const bool shouldPlayAudio =
      audioAllowed && now >= MINIMUM_VALID_TIME;
  appendLog(forecastAt, probabilityPercent, rainThreeHours,
            shouldPlayAudio, now);
  Serial.printf(
      "Rain forecast alert: %u %%, %.1f mm/3h, audio=%s\n",
      probabilityPercent, rainThreeHours,
      shouldPlayAudio ? "yes" : "no");

  if (!shouldPlayAudio) {
    return true;
  }

  char rainText[24];
  SpeechNumberFormatter::formatOneDecimal(rainThreeHours, rainText,
                                          sizeof(rainText));
  char message[192];
  snprintf(
      message, sizeof(message),
      "降雨予報のお知らせです。3時間以内に雨が降る可能性があります。"
      "降水確率は%uパーセント、予想雨量は%sミリです。",
      probabilityPercent, rainText);
  speech.playAlertTone();
  speech.speak(message);
  return true;
}

bool RainForecastAlertService::isActive() const { return active_; }

uint8_t RainForecastAlertService::probabilityPercent() const {
  return probabilityPercent_;
}

float RainForecastAlertService::rainThreeHours() const {
  return rainThreeHours_;
}
