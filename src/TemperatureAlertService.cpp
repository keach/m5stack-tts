#include "TemperatureAlertService.h"

#include <SD.h>
#include <time.h>

namespace {
constexpr char LOG_PATH[] = "/temperature_alerts.csv";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
}  // namespace

void TemperatureAlertService::begin() {
  preferences_.begin("temp_alert", false);
  for (AlertState& alert : alerts_) {
    alert.armed = preferences_.getBool(alert.armedKey, true);
    alert.lastAlertAt =
        static_cast<time_t>(preferences_.getULong64(alert.lastAlertKey, 0));
  }
}

void TemperatureAlertService::saveState(const AlertState& state) {
  preferences_.putBool(state.armedKey, state.armed);
  preferences_.putULong64(state.lastAlertKey,
                          static_cast<uint64_t>(state.lastAlertAt));
}

int TemperatureAlertService::activeThreshold(float temperature) const {
  if (temperature >= 35.0F) return 35;
  if (temperature >= 30.0F) return 30;
  return 0;
}

bool TemperatureAlertService::appendLog(float temperature, int threshold,
                                        bool audioPlayed, time_t alertTime) {
  const bool needsHeader = !SD.exists(LOG_PATH);
  File file = SD.open(LOG_PATH, FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open %s.\n", LOG_PATH);
    return false;
  }
  if (needsHeader) {
    file.println("datetime,temperature_c,threshold_c,audio_played");
  }

  char formattedTime[20] = "unknown";
  if (alertTime >= MINIMUM_VALID_TIME) {
    tm timeInfo = {};
    localtime_r(&alertTime, &timeInfo);
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S",
             &timeInfo);
  }
  const size_t written = file.printf("%s,%.1f,%d,%s\n", formattedTime,
                                     temperature, threshold,
                                     audioPlayed ? "true" : "false");
  file.close();
  return written > 0;
}

bool TemperatureAlertService::evaluate(float temperature, bool audioAllowed,
                                       SpeechService& speech) {
  const time_t now = time(nullptr);
  const bool timeIsValid = now >= MINIMUM_VALID_TIME;
  bool triggered[ALERT_COUNT] = {false, false};
  int highestTriggeredIndex = -1;

  for (int index = 0; index < ALERT_COUNT; ++index) {
    AlertState& alert = alerts_[index];
    if (!alert.armed && timeIsValid && alert.lastAlertAt < MINIMUM_VALID_TIME) {
      alert.lastAlertAt = now;
      saveState(alert);
    }
    if (!alert.armed && timeIsValid && alert.lastAlertAt >= MINIMUM_VALID_TIME &&
        now - alert.lastAlertAt >= REARM_DELAY_SECONDS &&
        temperature <= alert.threshold - 1.0F) {
      alert.armed = true;
      saveState(alert);
      Serial.printf("Temperature alert %d C rearmed.\n", alert.threshold);
    }

    if (alert.armed && temperature >= alert.threshold) {
      triggered[index] = true;
      highestTriggeredIndex = index;
      alert.armed = false;
      alert.lastAlertAt = timeIsValid ? now : 0;
      saveState(alert);
    }
  }

  if (highestTriggeredIndex < 0) {
    return false;
  }

  const bool shouldPlayAudio = audioAllowed && timeIsValid;
  for (int index = 0; index < ALERT_COUNT; ++index) {
    if (!triggered[index]) continue;
    const bool audioPlayed = shouldPlayAudio && index == highestTriggeredIndex;
    appendLog(temperature, alerts_[index].threshold, audioPlayed, now);
    Serial.printf("Temperature alert: %.1f C >= %d C, audio=%s\n",
                  temperature, alerts_[index].threshold,
                  audioPlayed ? "yes" : "no");
  }

  if (!shouldPlayAudio) {
    return false;
  }

  const int threshold = alerts_[highestTriggeredIndex].threshold;
  char message[160];
  snprintf(message, sizeof(message),
           threshold >= 35
               ? "高温警告です。現在の気温は%.1f度です。熱中症に十分注意してください。"
               : "高温注意です。現在の気温は%.1f度です。熱中症に注意してください。",
           temperature);
  speech.playAlertTone();
  speech.speak(message);
  return true;
}
