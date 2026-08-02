#pragma once

#include <Arduino.h>
#include <time.h>

enum class ThingSpeakPublishResult {
  NotAttempted,
  Sent,
  CredentialsMissing,
  WiFiDisconnected,
  TimeUnavailable,
  RequestFailed,
};

class ThingSpeakPublisher {
 public:
  ThingSpeakPublishResult publish(
      time_t observedAt, float temperature, int humidity, int pressure,
      int weatherConditionId, uint8_t precipitationProbability,
      int temperatureAlertThreshold, int wifiRssi, bool rainAlertActive);
};
