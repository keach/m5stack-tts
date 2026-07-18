#pragma once

#include <Arduino.h>
#include <time.h>

enum class AmbientPublishResult {
  NotAttempted,
  Sent,
  CredentialsMissing,
  WiFiDisconnected,
  TimeUnavailable,
  RequestFailed,
};

class AmbientPublisher {
 public:
  AmbientPublishResult publish(time_t observedAt, const char* condition,
                               float temperature, int humidity, int pressure,
                               float rainLastHour,
                               int temperatureAlertThreshold, bool rainingNow,
                               int wifiRssi);
};
