#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
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

 private:
  bool requestIntervalElapsed() const;
  ThingSpeakPublishResult postSingle(JsonDocument& record);
  ThingSpeakPublishResult sendQueuedBatch();

  unsigned long lastRequestStartedAt_ = 0;
  bool requestStarted_ = false;
};
