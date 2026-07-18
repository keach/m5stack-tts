#include "RainAlertService.h"

#include <SD.h>
#include <time.h>

#include "SpeechNumberFormatter.h"

namespace {
constexpr char NVS_NAMESPACE[] = "rain_alert";
constexpr char ACTIVE_KEY[] = "active";
constexpr char DRY_COUNT_KEY[] = "dry_count";
constexpr char LOG_PATH[] = "/rain_alerts.csv";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
}  // namespace

void RainAlertService::begin() {
  preferences_.begin(NVS_NAMESPACE, false);
  rainActive_ = preferences_.getBool(ACTIVE_KEY, false);
  dryObservations_ = min(
      preferences_.getUChar(DRY_COUNT_KEY, 0), DRY_OBSERVATIONS_TO_REARM);
}

void RainAlertService::saveState() {
  preferences_.putBool(ACTIVE_KEY, rainActive_);
  preferences_.putUChar(DRY_COUNT_KEY, dryObservations_);
}

bool RainAlertService::appendLog(const char* condition, float rainLastHour,
                                 bool audioPlayed, time_t alertTime) {
  const bool needsHeader = !SD.exists(LOG_PATH);
  File file = SD.open(LOG_PATH, FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open %s.\n", LOG_PATH);
    return false;
  }
  if (needsHeader) {
    file.println("datetime,weather,rain_1h_mm,audio_played");
  }

  char formattedTime[20] = "unknown";
  if (alertTime >= MINIMUM_VALID_TIME) {
    tm timeInfo = {};
    localtime_r(&alertTime, &timeInfo);
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S",
             &timeInfo);
  }
  const size_t written =
      file.printf("%s,%s,%.1f,%s\n", formattedTime, condition, rainLastHour,
                  audioPlayed ? "true" : "false");
  file.close();
  return written > 0;
}

void RainAlertService::evaluate(bool rainingNow, const char* condition,
                                float rainLastHour, bool audioAllowed,
                                SpeechService& speech) {
  if (!rainingNow) {
    if (!rainActive_) {
      return;
    }
    if (dryObservations_ < DRY_OBSERVATIONS_TO_REARM) {
      ++dryObservations_;
      saveState();
    }
    if (dryObservations_ >= DRY_OBSERVATIONS_TO_REARM) {
      rainActive_ = false;
      dryObservations_ = 0;
      saveState();
      Serial.println("Rain alert rearmed after two dry observations.");
    }
    return;
  }

  if (rainActive_) {
    if (dryObservations_ != 0) {
      dryObservations_ = 0;
      saveState();
    }
    return;
  }

  rainActive_ = true;
  dryObservations_ = 0;
  saveState();

  const time_t now = time(nullptr);
  const bool shouldPlayAudio =
      audioAllowed && now >= MINIMUM_VALID_TIME;
  appendLog(condition, rainLastHour, shouldPlayAudio, now);
  Serial.printf("Rain alert: %s, %.1f mm in the last hour, audio=%s\n",
                condition, rainLastHour, shouldPlayAudio ? "yes" : "no");

  if (!shouldPlayAudio) {
    return;
  }

  char rainText[24];
  SpeechNumberFormatter::formatOneDecimal(rainLastHour, rainText,
                                          sizeof(rainText));
  char message[160];
  snprintf(message, sizeof(message),
           "降雨のお知らせです。雨が降っています。直近1時間の雨量は%sミリです。",
           rainText);
  speech.playAlertTone();
  speech.speak(message);
}

bool RainAlertService::isRainActive() const { return rainActive_; }
