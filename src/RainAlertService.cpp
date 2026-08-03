#include "RainAlertService.h"

#include <SD.h>
#include <time.h>

#include "SdCardLock.h"

#include "SpeechNumberFormatter.h"

namespace {
constexpr char NVS_NAMESPACE[] = "rain_alert";
constexpr char ACTIVE_KEY[] = "active";
constexpr char DRY_COUNT_KEY[] = "dry_count";
constexpr char LOG_PATH[] = "/rain_alerts.csv";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr unsigned long LOG_RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr uint8_t LOG_RETRY_LIMIT = 3;
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
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    Serial.println("SD card is busy; alert log was skipped.");
    return false;
  }

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

bool RainAlertService::evaluate(bool rainingNow, const char* condition,
                                float rainLastHour, bool audioAllowed,
                                bool* audioRequested) {
  if (audioRequested != nullptr) {
    *audioRequested = false;
  }
  if (!rainingNow) {
    if (!rainActive_) {
      return false;
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
    return false;
  }

  if (rainActive_) {
    if (dryObservations_ != 0) {
      dryObservations_ = 0;
      saveState();
    }
    return false;
  }

  rainActive_ = true;
  dryObservations_ = 0;
  saveState();

  const time_t now = time(nullptr);
  const bool shouldPlayAudio =
      audioAllowed && now >= MINIMUM_VALID_TIME;
  if (!appendLog(condition, rainLastHour, shouldPlayAudio, now)) {
    scheduleLogRetry(condition, rainLastHour, shouldPlayAudio, now);
  }
  Serial.printf("Rain alert: %s, %.1f mm in the last hour, audio=%s\n",
                condition, rainLastHour, shouldPlayAudio ? "yes" : "no");

  if (audioRequested != nullptr) {
    *audioRequested = shouldPlayAudio;
  }
  return true;
}

void RainAlertService::notify(float rainLastHour, SpeechService& speech) {
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

void RainAlertService::scheduleLogRetry(const char* condition,
                                             float rainLastHour,
                                             bool audioPlayed,
                                             time_t alertTime) {
  if (pendingLog_.active) {
    Serial.println("Rain alert log retry is already pending.");
    return;
  }
  strncpy(pendingLog_.condition, condition, sizeof(pendingLog_.condition) - 1);
  pendingLog_.condition[sizeof(pendingLog_.condition) - 1] = '\0';
  pendingLog_.rainLastHour = rainLastHour;
  pendingLog_.audioPlayed = audioPlayed;
  pendingLog_.alertTime = alertTime;
  pendingLog_.nextRetryAt = millis() + LOG_RETRY_INTERVAL_MS;
  pendingLog_.retryCount = 0;
  pendingLog_.active = true;
}
void RainAlertService::processPendingLog() {
  if (!pendingLog_.active ||
      static_cast<long>(millis() - pendingLog_.nextRetryAt) < 0) return;
  if (appendLog(pendingLog_.condition, pendingLog_.rainLastHour,
                pendingLog_.audioPlayed, pendingLog_.alertTime)) {
    pendingLog_.active = false;
    return;
  }
  ++pendingLog_.retryCount;
  if (pendingLog_.retryCount >= LOG_RETRY_LIMIT) {
    pendingLog_.active = false;
    Serial.println("Rain alert log retry limit reached.");
    return;
  }
  pendingLog_.nextRetryAt = millis() + LOG_RETRY_INTERVAL_MS;
}
bool RainAlertService::isRainActive() const { return rainActive_; }
