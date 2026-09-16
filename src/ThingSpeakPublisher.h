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
  void handle();
  bool hasPendingRecords() const { return pendingCount_ > 0; }
  ThingSpeakPublishResult publish(
      time_t observedAt, float temperature, int humidity, int pressure,
      int weatherConditionId, uint8_t precipitationProbability,
      int temperatureAlertThreshold, int wifiRssi, bool rainAlertActive);

 private:
  bool requestIntervalElapsed() const;
  bool queueOrPreserve(JsonDocument& record);
  bool preserveInRam(JsonDocument& record);
  ThingSpeakPublishResult postSingle(JsonDocument& record);
  ThingSpeakPublishResult sendQueuedBatch();

  static constexpr size_t RAM_PENDING_CAPACITY = 10;
  JsonDocument pendingRecords_[RAM_PENDING_CAPACITY];
  size_t pendingHead_ = 0;
  size_t pendingCount_ = 0;
  unsigned long lastPendingRetryAt_ = 0;
  unsigned long lastRequestStartedAt_ = 0;
  bool requestStarted_ = false;
};
